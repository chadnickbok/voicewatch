import AppKit
import AVFoundation
import CoreImage
import ImageIO
import UniformTypeIdentifiers
import CUVCControl

struct FrameStats {
    let percentile95: Int
    let clippedPercent: Double
}

struct UVCIntegerRange {
    let minimum: Int
    let maximum: Int
    let current: Int
    let defaultValue: Int
}

final class UVCCameraControl {
    private var handle: OpaquePointer?

    init(vendorID: Int32, productID: Int32) throws {
        var newHandle: OpaquePointer?
        try Self.check(
            cleancam_uvc_open(UInt16(vendorID), UInt16(productID), &newHandle),
            operation: "open USB camera controls"
        )
        handle = newHandle
    }

    deinit {
        if let handle { cleancam_uvc_close(handle) }
    }

    func exposureRange() -> UVCIntegerRange? {
        guard let handle else { return nil }
        var range = CleanCamUVCRange()
        guard cleancam_uvc_get_exposure(handle, &range) == 0 else { return nil }
        return UVCIntegerRange(
            minimum: Int(range.minimum),
            maximum: Int(range.maximum),
            current: Int(range.current),
            defaultValue: Int(range.default_value)
        )
    }

    func gainRange() -> UVCIntegerRange? {
        guard let handle else { return nil }
        var range = CleanCamUVCRange()
        guard cleancam_uvc_get_gain(handle, &range) == 0 else { return nil }
        return UVCIntegerRange(
            minimum: Int(range.minimum),
            maximum: Int(range.maximum),
            current: Int(range.current),
            defaultValue: Int(range.default_value)
        )
    }

    func automaticExposureEnabled() -> Bool? {
        guard let handle else { return nil }
        var enabled = false
        guard cleancam_uvc_get_auto_exposure(handle, &enabled) == 0 else { return nil }
        return enabled
    }

    func setAutomaticExposure(_ enabled: Bool) throws {
        guard let handle else { throw ControlError("USB camera control is closed.") }
        try Self.check(
            cleancam_uvc_set_auto_exposure(handle, enabled),
            operation: enabled ? "enable automatic exposure" : "enable manual exposure"
        )
    }

    func setExposure(_ value: Int) throws {
        guard let handle else {
            throw ControlError("Hardware exposure is unavailable.")
        }
        let clamped = UInt32(max(1, value))
        try Self.check(cleancam_uvc_set_exposure(handle, clamped), operation: "set exposure")
    }

    func setGain(_ value: Int) throws {
        guard let handle, let range = gainRange() else {
            throw ControlError("Hardware gain is unavailable.")
        }
        let clamped = UInt16(min(max(value, range.minimum), range.maximum))
        try Self.check(cleancam_uvc_set_gain(handle, clamped), operation: "set gain")
    }

    func disableBacklightCompensation() {
        guard let handle else { return }
        _ = cleancam_uvc_disable_backlight_compensation(handle)
    }

    private static func check(_ result: Int32, operation: String) throws {
        guard result == 0 else {
            let detail = String(cString: cleancam_uvc_error_string(result))
            throw ControlError("Could not \(operation): \(detail)")
        }
    }

    struct ControlError: LocalizedError {
        let message: String
        init(_ message: String) { self.message = message }
        var errorDescription: String? { message }
    }
}

final class CameraController: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    let session = AVCaptureSession()
    var onStatus: ((String) -> Void)?
    var onStats: ((FrameStats) -> Void)?
    var onDeviceChanged: ((AVCaptureDevice) -> Void)?
    var onCaptureSaved: ((URL) -> Void)?

    private let sessionQueue = DispatchQueue(label: "CleanCam.session")
    private let videoQueue = DispatchQueue(label: "CleanCam.frames")
    private let output = AVCaptureVideoDataOutput()
    private let ciContext = CIContext()
    private var input: AVCaptureDeviceInput?
    private var frameNumber = 0
    private var captureRequested = false
    private var requestedCaptureURL: URL?
    private var normalizedROI = CGRect(x: 0.2, y: 0.15, width: 0.6, height: 0.7)
    private(set) var uvcControl: UVCCameraControl?

    var device: AVCaptureDevice? { input?.device }

    static func cameras() -> [AVCaptureDevice] {
        AVCaptureDevice.DiscoverySession(
            deviceTypes: [.external, .builtInWideAngleCamera, .continuityCamera],
            mediaType: .video,
            position: .unspecified
        ).devices
    }

    func start(preferredName: String = "Logitech StreamCam") {
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized:
            configure(preferredName: preferredName)
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { [weak self] granted in
                guard let self else { return }
                if granted {
                    self.configure(preferredName: preferredName)
                } else {
                    self.report("Camera permission was declined. Enable it in System Settings → Privacy & Security → Camera.")
                }
            }
        default:
            report("Camera access is unavailable. Enable CleanCam in System Settings → Privacy & Security → Camera.")
        }
    }

    private func configure(preferredName: String) {
        sessionQueue.async { [weak self] in
            guard let self else { return }
            let cameras = Self.cameras()
            guard let camera = cameras.first(where: { $0.localizedName == preferredName }) ?? cameras.first else {
                self.report("No video cameras were found.")
                return
            }

            self.session.beginConfiguration()
            self.session.sessionPreset = .high

            do {
                let newInput = try AVCaptureDeviceInput(device: camera)
                guard self.session.canAddInput(newInput) else {
                    self.session.commitConfiguration()
                    self.report("Could not attach \(camera.localizedName).")
                    return
                }
                self.session.addInput(newInput)
                self.input = newInput
                self.openUVCControls(for: camera)

                self.output.alwaysDiscardsLateVideoFrames = true
                self.output.videoSettings = [
                    kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA
                ]
                self.output.setSampleBufferDelegate(self, queue: self.videoQueue)
                guard self.session.canAddOutput(self.output) else {
                    self.session.commitConfiguration()
                    self.report("Could not create the video output.")
                    return
                }
                self.session.addOutput(self.output)

                if let connection = self.output.connection(with: .video),
                   connection.isVideoRotationAngleSupported(0) {
                    connection.videoRotationAngle = 0
                }

                self.session.commitConfiguration()
                self.session.startRunning()
                DispatchQueue.main.async {
                    self.onDeviceChanged?(camera)
                }
                self.report("Live: \(camera.localizedName). Drag a box around the CoreS3 screen, then enable Auto-tune.")
            } catch {
                self.session.commitConfiguration()
                self.report("Camera setup failed: \(error.localizedDescription)")
            }
        }
    }

    func switchCamera(to camera: AVCaptureDevice) {
        sessionQueue.async { [weak self] in
            guard let self else { return }
            do {
                let newInput = try AVCaptureDeviceInput(device: camera)
                self.session.beginConfiguration()
                if let oldInput = self.input {
                    self.session.removeInput(oldInput)
                }
                if self.session.canAddInput(newInput) {
                    self.session.addInput(newInput)
                    self.input = newInput
                    self.openUVCControls(for: camera)
                    self.session.commitConfiguration()
                    DispatchQueue.main.async {
                        self.onDeviceChanged?(camera)
                    }
                    self.report("Live: \(camera.localizedName)")
                } else {
                    if let oldInput = self.input, self.session.canAddInput(oldInput) {
                        self.session.addInput(oldInput)
                    }
                    self.session.commitConfiguration()
                    self.report("Could not switch to \(camera.localizedName).")
                }
            } catch {
                self.report("Camera switch failed: \(error.localizedDescription)")
            }
        }
    }

    func setROI(_ roi: CGRect) {
        videoQueue.async { [weak self] in
            self?.normalizedROI = roi
        }
    }

    func requestCapture(to url: URL? = nil) {
        videoQueue.async { [weak self] in
            self?.requestedCaptureURL = url
            self?.captureRequested = true
        }
        report("Capturing the next full-resolution frame…")
    }

    func setContinuousAutoExposure() {
        sessionQueue.async { [weak self] in
            guard let self, let control = self.uvcControl else {
                self?.report("USB hardware exposure controls are unavailable for this camera.")
                return
            }
            do {
                try control.setAutomaticExposure(true)
                self.report("UVC automatic exposure enabled.")
                if let device = self.input?.device {
                    DispatchQueue.main.async { self.onDeviceChanged?(device) }
                }
            } catch {
                self.report(error.localizedDescription)
            }
        }
    }

    func lockExposure() {
        sessionQueue.async { [weak self] in
            guard let self, let control = self.uvcControl else {
                self?.report("USB hardware exposure controls are unavailable for this camera.")
                return
            }
            do {
                try control.setAutomaticExposure(false)
                self.report("UVC exposure locked at the current hardware setting.")
                if let device = self.input?.device {
                    DispatchQueue.main.async { self.onDeviceChanged?(device) }
                }
            } catch {
                self.report(error.localizedDescription)
            }
        }
    }

    func setExposure(_ value: Int) {
        sessionQueue.async { [weak self] in
            guard let self, let control = self.uvcControl else { return }
            do {
                try control.setExposure(value)
                if let device = self.input?.device {
                    DispatchQueue.main.async { self.onDeviceChanged?(device) }
                }
            } catch {
                self.report(error.localizedDescription)
            }
        }
    }

    func setGain(_ value: Int) {
        sessionQueue.async { [weak self] in
            guard let self, let control = self.uvcControl else { return }
            do {
                try control.setGain(value)
                if let device = self.input?.device {
                    DispatchQueue.main.async { self.onDeviceChanged?(device) }
                }
            } catch {
                self.report(error.localizedDescription)
            }
        }
    }

    private func openUVCControls(for device: AVCaptureDevice) {
        uvcControl = nil
        guard let ids = Self.usbIDs(from: device.modelID) else {
            report("Live: \(device.localizedName). It is not an external UVC camera, so hardware controls are unavailable.")
            return
        }
        do {
            uvcControl = try UVCCameraControl(vendorID: ids.vendor, productID: ids.product)
        } catch {
            report("Video is available, but UVC controls could not open: \(error.localizedDescription)")
        }
    }

    static func usbIDs(from modelID: String) -> (vendor: Int32, product: Int32)? {
        let pattern = #"VendorID_(\d+)\s+ProductID_(\d+)"#
        guard let regex = try? NSRegularExpression(pattern: pattern),
              let match = regex.firstMatch(in: modelID, range: NSRange(modelID.startIndex..., in: modelID)),
              let vendorRange = Range(match.range(at: 1), in: modelID),
              let productRange = Range(match.range(at: 2), in: modelID),
              let vendor = Int32(modelID[vendorRange]),
              let product = Int32(modelID[productRange]) else { return nil }
        return (vendor, product)
    }

    private func report(_ text: String) {
        DispatchQueue.main.async { [weak self] in self?.onStatus?(text) }
    }

    func captureOutput(
        _ output: AVCaptureOutput,
        didOutput sampleBuffer: CMSampleBuffer,
        from connection: AVCaptureConnection
    ) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }

        frameNumber += 1
        if frameNumber % 8 == 0 {
            let stats = meter(pixelBuffer)
            DispatchQueue.main.async { [weak self] in self?.onStats?(stats) }
        }

        guard captureRequested else { return }
        captureRequested = false
        save(pixelBuffer)
    }

    private func meter(_ pixelBuffer: CVPixelBuffer) -> FrameStats {
        CVPixelBufferLockBaseAddress(pixelBuffer, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, .readOnly) }

        guard let base = CVPixelBufferGetBaseAddress(pixelBuffer) else {
            return FrameStats(percentile95: 0, clippedPercent: 0)
        }

        let width = CVPixelBufferGetWidth(pixelBuffer)
        let height = CVPixelBufferGetHeight(pixelBuffer)
        let rowBytes = CVPixelBufferGetBytesPerRow(pixelBuffer)
        let roi = normalizedROI.standardized
        let x0 = max(0, min(width - 1, Int(CGFloat(width) * roi.minX)))
        let y0 = max(0, min(height - 1, Int(CGFloat(height) * roi.minY)))
        let x1 = max(x0 + 1, min(width, Int(CGFloat(width) * roi.maxX)))
        let y1 = max(y0 + 1, min(height, Int(CGFloat(height) * roi.maxY)))
        let step = max(2, min(width, height) / 300)
        let bytes = base.assumingMemoryBound(to: UInt8.self)
        var histogram = Array(repeating: 0, count: 256)
        var samples = 0
        var clipped = 0

        for y in stride(from: y0, to: y1, by: step) {
            let row = bytes.advanced(by: y * rowBytes)
            for x in stride(from: x0, to: x1, by: step) {
                let p = row.advanced(by: x * 4)
                let b = Double(p[0])
                let g = Double(p[1])
                let r = Double(p[2])
                let luma = max(0, min(255, Int(0.0722 * b + 0.7152 * g + 0.2126 * r)))
                histogram[luma] += 1
                samples += 1
                if luma >= 250 {
                    clipped += 1
                }
            }
        }

        let threshold = Int(Double(samples) * 0.95)
        var cumulative = 0
        var p95 = 255
        for value in 0..<256 {
            cumulative += histogram[value]
            if cumulative >= threshold {
                p95 = value
                break
            }
        }
        return FrameStats(
            percentile95: p95,
            clippedPercent: samples == 0 ? 0 : (Double(clipped) / Double(samples)) * 100
        )
    }

    private func save(_ pixelBuffer: CVPixelBuffer) {
        let image = CIImage(cvPixelBuffer: pixelBuffer)
        guard let cgImage = ciContext.createCGImage(image, from: image.extent) else {
            report("Could not render the captured frame.")
            return
        }

        let url: URL
        if let requestedCaptureURL {
            url = requestedCaptureURL
            self.requestedCaptureURL = nil
        } else {
            let formatter = DateFormatter()
            formatter.dateFormat = "yyyyMMdd-HHmmss-SSS"
            let folder = FileManager.default.homeDirectoryForCurrentUser
                .appendingPathComponent("Pictures", isDirectory: true)
                .appendingPathComponent("CleanCam", isDirectory: true)
            url = folder.appendingPathComponent("cores3-\(formatter.string(from: Date())).png")
        }
        let folder = url.deletingLastPathComponent()

        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            guard let destination = CGImageDestinationCreateWithURL(
                url as CFURL,
                UTType.png.identifier as CFString,
                1,
                nil
            ) else {
                report("Could not create the PNG destination.")
                return
            }
            CGImageDestinationAddImage(destination, cgImage, nil)
            guard CGImageDestinationFinalize(destination) else {
                report("PNG encoding failed.")
                return
            }
            report("Saved \(url.path)")
            DispatchQueue.main.async { [weak self] in self?.onCaptureSaved?(url) }
        } catch {
            report("Capture failed: \(error.localizedDescription)")
        }
    }
}

final class PreviewView: NSView {
    let previewLayer: AVCaptureVideoPreviewLayer
    var onROIChanged: ((CGRect) -> Void)?

    private let selectionLayer = CAShapeLayer()
    private var dragStart: CGPoint?
    private var selection = CGRect.zero

    init(session: AVCaptureSession) {
        previewLayer = AVCaptureVideoPreviewLayer(session: session)
        super.init(frame: .zero)
        wantsLayer = true
        layer = CALayer()
        previewLayer.videoGravity = .resizeAspect
        layer?.addSublayer(previewLayer)

        selectionLayer.strokeColor = NSColor.systemCyan.cgColor
        selectionLayer.fillColor = NSColor.systemCyan.withAlphaComponent(0.08).cgColor
        selectionLayer.lineWidth = 2
        selectionLayer.lineDashPattern = [7, 5]
        layer?.addSublayer(selectionLayer)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func layout() {
        super.layout()
        previewLayer.frame = bounds
        selectionLayer.frame = bounds
        if selection == .zero {
            selection = bounds.insetBy(dx: bounds.width * 0.2, dy: bounds.height * 0.15)
        }
        redrawSelection()
    }

    override func mouseDown(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        dragStart = point
        selection = CGRect(origin: point, size: .zero)
        redrawSelection()
    }

    override func mouseDragged(with event: NSEvent) {
        guard let start = dragStart else { return }
        let point = convert(event.locationInWindow, from: nil)
        selection = CGRect(
            x: min(start.x, point.x),
            y: min(start.y, point.y),
            width: abs(point.x - start.x),
            height: abs(point.y - start.y)
        ).intersection(bounds)
        redrawSelection()
    }

    override func mouseUp(with event: NSEvent) {
        dragStart = nil
        guard selection.width >= 20, selection.height >= 20 else { return }
        let converted = previewLayer.metadataOutputRectConverted(fromLayerRect: selection)
        onROIChanged?(converted)
    }

    private func redrawSelection() {
        selectionLayer.path = CGPath(rect: selection, transform: nil)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private let controller = CameraController()
    private var window: NSWindow!
    private var preview: PreviewView!
    private var cameraPopup: NSPopUpButton!
    private var statusLabel: NSTextField!
    private var meterLabel: NSTextField!
    private var exposureSlider: NSSlider!
    private var exposureLabel: NSTextField!
    private var gainSlider: NSSlider!
    private var gainLabel: NSTextField!
    private var autoTuneButton: NSButton!
    private var autoTuneEnabled = false
    private var lastTune = Date.distantPast
    private var cameras: [AVCaptureDevice] = []
    private var activeDevice: AVCaptureDevice?

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildUI()
        wireController()
        NSApp.activate(ignoringOtherApps: true)
        window.makeKeyAndOrderFront(nil)
        controller.start()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    private func buildUI() {
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1180, height: 720),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "CleanCam — CoreS3 Capture"
        window.center()
        window.minSize = NSSize(width: 900, height: 560)

        let root = NSView()
        root.translatesAutoresizingMaskIntoConstraints = false
        window.contentView = root

        preview = PreviewView(session: controller.session)
        preview.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(preview)

        let sidebar = NSStackView()
        sidebar.orientation = .vertical
        sidebar.alignment = .leading
        sidebar.spacing = 10
        sidebar.edgeInsets = NSEdgeInsets(top: 20, left: 18, bottom: 18, right: 18)
        sidebar.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(sidebar)

        let title = NSTextField(labelWithString: "CoreS3 screen capture")
        title.font = .systemFont(ofSize: 20, weight: .semibold)
        sidebar.addArrangedSubview(title)
        sidebar.addArrangedSubview(label("Camera"))

        cameraPopup = NSPopUpButton()
        cameraPopup.target = self
        cameraPopup.action = #selector(cameraChanged)
        cameraPopup.widthAnchor.constraint(equalToConstant: 265).isActive = true
        sidebar.addArrangedSubview(cameraPopup)

        sidebar.addArrangedSubview(separator())
        sidebar.addArrangedSubview(label("Hardware exposure"))

        let autoExposure = button("Reset to continuous auto", #selector(resetAutoExposure))
        sidebar.addArrangedSubview(autoExposure)
        let lockExposure = button("Lock current exposure", #selector(lockExposureNow))
        sidebar.addArrangedSubview(lockExposure)

        exposureLabel = label("Exposure time: —")
        sidebar.addArrangedSubview(exposureLabel)
        exposureSlider = NSSlider(value: 0, minValue: 0, maxValue: 1, target: self, action: #selector(exposureChanged))
        exposureSlider.isContinuous = true
        exposureSlider.widthAnchor.constraint(equalToConstant: 265).isActive = true
        sidebar.addArrangedSubview(exposureSlider)

        gainLabel = label("Sensor gain: —")
        sidebar.addArrangedSubview(gainLabel)
        gainSlider = NSSlider(value: 0, minValue: 0, maxValue: 1, target: self, action: #selector(gainChanged))
        gainSlider.isContinuous = true
        gainSlider.widthAnchor.constraint(equalToConstant: 265).isActive = true
        sidebar.addArrangedSubview(gainSlider)

        sidebar.addArrangedSubview(separator())
        autoTuneButton = button("Start display auto-tune", #selector(toggleAutoTune))
        autoTuneButton.bezelStyle = .rounded
        sidebar.addArrangedSubview(autoTuneButton)

        meterLabel = label("Meter: waiting for frames…")
        meterLabel.textColor = .secondaryLabelColor
        sidebar.addArrangedSubview(meterLabel)

        let hint = label("Drag a cyan box tightly around the CoreS3 display. Auto-tune lowers exposure until bright pixels retain detail.")
        hint.maximumNumberOfLines = 4
        hint.lineBreakMode = .byWordWrapping
        hint.preferredMaxLayoutWidth = 265
        hint.textColor = .secondaryLabelColor
        sidebar.addArrangedSubview(hint)

        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .vertical)
        sidebar.addArrangedSubview(spacer)

        let capture = button("Capture full-resolution PNG", #selector(captureFrame))
        capture.bezelStyle = .rounded
        capture.keyEquivalent = "\r"
        sidebar.addArrangedSubview(capture)

        statusLabel = label("Starting camera…")
        statusLabel.maximumNumberOfLines = 4
        statusLabel.lineBreakMode = .byWordWrapping
        statusLabel.preferredMaxLayoutWidth = 265
        statusLabel.textColor = .secondaryLabelColor
        sidebar.addArrangedSubview(statusLabel)

        NSLayoutConstraint.activate([
            root.leadingAnchor.constraint(equalTo: window.contentView!.leadingAnchor),
            root.trailingAnchor.constraint(equalTo: window.contentView!.trailingAnchor),
            root.topAnchor.constraint(equalTo: window.contentView!.topAnchor),
            root.bottomAnchor.constraint(equalTo: window.contentView!.bottomAnchor),
            preview.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            preview.topAnchor.constraint(equalTo: root.topAnchor),
            preview.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            preview.trailingAnchor.constraint(equalTo: sidebar.leadingAnchor),
            preview.widthAnchor.constraint(greaterThanOrEqualToConstant: 580),
            sidebar.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            sidebar.topAnchor.constraint(equalTo: root.topAnchor),
            sidebar.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            sidebar.widthAnchor.constraint(equalToConstant: 305)
        ])
    }

    private func wireController() {
        cameras = CameraController.cameras()
        cameraPopup.removeAllItems()
        cameraPopup.addItems(withTitles: cameras.map(\.localizedName))

        preview.onROIChanged = { [weak self] roi in
            self?.controller.setROI(roi)
            self?.statusLabel.stringValue = "Metering the selected display region."
        }
        controller.onStatus = { [weak self] text in
            self?.statusLabel.stringValue = text
        }
        controller.onDeviceChanged = { [weak self] device in
            self?.refreshControls(for: device)
        }
        controller.onStats = { [weak self] stats in
            self?.handle(stats)
        }
    }

    private func refreshControls(for device: AVCaptureDevice) {
        activeDevice = device
        cameraPopup.selectItem(withTitle: device.localizedName)

        if let range = controller.uvcControl?.exposureRange() {
            exposureSlider.isEnabled = true
            exposureSlider.minValue = log2(Double(max(1, range.minimum)))
            exposureSlider.maxValue = log2(Double(max(1, range.maximum)))
            exposureSlider.doubleValue = log2(Double(max(1, range.current)))
            updateExposureLabel(value: range.current)
        } else {
            exposureSlider.isEnabled = false
            exposureLabel.stringValue = "Exposure time: unavailable"
        }

        if let range = controller.uvcControl?.gainRange() {
            gainSlider.isEnabled = true
            gainSlider.minValue = Double(range.minimum)
            gainSlider.maxValue = Double(range.maximum)
            gainSlider.doubleValue = Double(range.current)
            gainLabel.stringValue = "Sensor gain: \(range.current)"
        } else {
            gainSlider.isEnabled = false
            gainLabel.stringValue = "Sensor gain: unavailable"
        }
    }

    private func handle(_ stats: FrameStats) {
        meterLabel.stringValue = String(
            format: "Meter: p95 %d/255 · clipped %.2f%%",
            stats.percentile95,
            stats.clippedPercent
        )
        guard autoTuneEnabled, Date().timeIntervalSince(lastTune) > 1.0,
              exposureSlider.isEnabled else { return }

        lastTune = Date()
        let current = Int(pow(2, exposureSlider.doubleValue).rounded())
        let minimum = Int(pow(2, exposureSlider.minValue).rounded())
        let maximum = Int(pow(2, exposureSlider.maxValue).rounded())
        var next = current
        if stats.clippedPercent > 1.0 || stats.percentile95 > 225 {
            next = Int(Double(current) * (stats.clippedPercent > 5 ? 0.65 : 0.78))
        } else if stats.percentile95 < 145 {
            next = Int(Double(current) * 1.35)
        } else {
            autoTuneEnabled = false
            autoTuneButton.title = "Start display auto-tune"
            controller.lockExposure()
            statusLabel.stringValue = "Display exposure tuned and locked."
            return
        }
        next = min(max(next, minimum), maximum)
        exposureSlider.doubleValue = log2(Double(max(1, next)))
        updateExposureLabel(value: next)
        controller.setExposure(next)
    }

    @objc private func cameraChanged() {
        let index = cameraPopup.indexOfSelectedItem
        guard cameras.indices.contains(index) else { return }
        controller.switchCamera(to: cameras[index])
    }

    @objc private func resetAutoExposure() {
        autoTuneEnabled = false
        autoTuneButton.title = "Start display auto-tune"
        controller.setContinuousAutoExposure()
    }

    @objc private func lockExposureNow() {
        autoTuneEnabled = false
        autoTuneButton.title = "Start display auto-tune"
        controller.lockExposure()
    }

    @objc private func exposureChanged() {
        let value = Int(pow(2, exposureSlider.doubleValue).rounded())
        updateExposureLabel(value: value)
        controller.setExposure(value)
    }

    @objc private func gainChanged() {
        let value = Int(gainSlider.doubleValue.rounded())
        gainLabel.stringValue = "Sensor gain: \(value)"
        controller.setGain(value)
    }

    private func updateExposureLabel(value: Int) {
        let seconds = Double(value) / 10_000.0
        if seconds < 1 {
            exposureLabel.stringValue = String(format: "Exposure time: 1/%.0f s", 1 / seconds)
        } else {
            exposureLabel.stringValue = String(format: "Exposure time: %.2f s", seconds)
        }
    }

    @objc private func toggleAutoTune() {
        guard exposureSlider.isEnabled else {
            statusLabel.stringValue = "The webcam did not expose UVC hardware exposure controls."
            return
        }
        autoTuneEnabled.toggle()
        autoTuneButton.title = autoTuneEnabled ? "Stop display auto-tune" : "Start display auto-tune"
        if autoTuneEnabled {
            controller.uvcControl?.disableBacklightCompensation()
            if gainSlider.isEnabled {
                let cleanGain = Int(gainSlider.minValue.rounded())
                gainSlider.doubleValue = Double(cleanGain)
                gainLabel.stringValue = "Sensor gain: \(cleanGain)"
                controller.setGain(cleanGain)
            }
            controller.lockExposure()
            statusLabel.stringValue = "Auto-tuning exposure from the cyan region…"
        }
    }

    @objc private func captureFrame() {
        controller.requestCapture()
    }

    private func label(_ text: String) -> NSTextField {
        NSTextField(labelWithString: text)
    }

    private func button(_ title: String, _ action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.alignment = .left
        return button
    }

    private func separator() -> NSBox {
        let box = NSBox()
        box.boxType = .separator
        box.widthAnchor.constraint(equalToConstant: 265).isActive = true
        return box
    }
}

func printCameraProbe() {
    let cameras = CameraController.cameras()
    if cameras.isEmpty {
        print("No cameras found")
        return
    }
    for camera in cameras {
        print("\(camera.localizedName)\n  id: \(camera.uniqueID)\n  model: \(camera.modelID)")
        guard let ids = CameraController.usbIDs(from: camera.modelID) else {
            print("  UVC controls: not an external USB camera")
            continue
        }
        do {
            let control = try UVCCameraControl(vendorID: ids.vendor, productID: ids.product)
            if let exposure = control.exposureRange() {
                print("  exposure (100 µs units): \(exposure.minimum)...\(exposure.maximum), current \(exposure.current), default \(exposure.defaultValue)")
            } else {
                print("  exposure: unavailable")
            }
            if let gain = control.gainRange() {
                print("  gain: \(gain.minimum)...\(gain.maximum), current \(gain.current), default \(gain.defaultValue)")
            } else {
                print("  gain: unavailable")
            }
            print("  automatic exposure: \(control.automaticExposureEnabled().map(String.init) ?? "unknown")")
        } catch {
            print("  UVC controls: \(error.localizedDescription)")
        }
    }
}

final class HeadlessCaptureDelegate: NSObject, NSApplicationDelegate {
    private let controller = CameraController()
    private let outputURL: URL
    private var captureScheduled = false

    init(outputURL: URL) {
        self.outputURL = outputURL
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        controller.onStatus = { [weak self] status in
            print(status)
            guard let self,
                  status.hasPrefix("Live:"),
                  !self.captureScheduled else { return }
            self.captureScheduled = true
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                self.controller.requestCapture(to: self.outputURL)
            }
        }
        controller.onCaptureSaved = { _ in NSApp.terminate(nil) }
        controller.start()
        DispatchQueue.main.asyncAfter(deadline: .now() + 12.0) {
            fputs("Timed out waiting for a camera frame.\n", stderr)
            NSApp.terminate(nil)
        }
    }
}

if CommandLine.arguments.contains("--probe") {
    printCameraProbe()
} else if CommandLine.arguments.contains("--reset-camera") {
    let result = cleancam_uvc_reset_device(1133, 2195)
    if result == 0 {
        print("Logitech StreamCam USB reset requested.")
    } else {
        fputs(
            "Camera reset failed: \(String(cString: cleancam_uvc_error_string(result))) (\(result))\n",
            stderr
        )
        exit(1)
    }
} else if let captureIndex = CommandLine.arguments.firstIndex(of: "--capture"),
          CommandLine.arguments.indices.contains(captureIndex + 1) {
    let app = NSApplication.shared
    let outputURL = URL(
        fileURLWithPath: CommandLine.arguments[captureIndex + 1],
        relativeTo: URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
    ).standardizedFileURL
    let delegate = HeadlessCaptureDelegate(outputURL: outputURL)
    app.delegate = delegate
    app.setActivationPolicy(.regular)
    withExtendedLifetime(delegate) {
        app.run()
    }
} else {
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.setActivationPolicy(.regular)
    app.run()
}
