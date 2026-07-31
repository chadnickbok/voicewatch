import CoreGraphics
import CoreImage
import Foundation

private struct Options {
    var input: URL
    var output: URL
    var blurRadius = 2.0
    var unsharpRadius = 2.5
    var unsharpIntensity = 0.45
}

private enum CommandError: Error, CustomStringConvertible {
    case usage(String)
    case invalidNumber(String, String)
    case unreadableImage(URL)
    case unavailableFilter(String)
    case unavailableColorSpace

    var description: String {
        switch self {
        case .usage(let message):
            return message
        case .invalidNumber(let flag, let value):
            return "\(flag) requires a non-negative number, got \(value)"
        case .unreadableImage(let url):
            return "could not read image: \(url.path)"
        case .unavailableFilter(let name):
            return "Core Image filter is unavailable: \(name)"
        case .unavailableColorSpace:
            return "could not create the sRGB output color space"
        }
    }
}

private let usage = """
Usage:
  moire-filter INPUT OUTPUT [options]

Options:
  --blur-radius N         Gaussian low-pass radius (default: 2.0)
  --unsharp-radius N      Unsharp-mask radius (default: 2.5)
  --unsharp-intensity N   Unsharp-mask amount (default: 0.45)
  --help                  Show this help

The output keeps the input dimensions. Use zero to disable either stage.
"""

private func nonNegative(_ value: String, flag: String) throws -> Double {
    guard let parsed = Double(value), parsed.isFinite, parsed >= 0 else {
        throw CommandError.invalidNumber(flag, value)
    }
    return parsed
}

private func parse(_ arguments: [String]) throws -> Options {
    if arguments.contains("--help") {
        print(usage)
        Foundation.exit(EXIT_SUCCESS)
    }
    guard arguments.count >= 2 else {
        throw CommandError.usage(usage)
    }
    var result = Options(
        input: URL(fileURLWithPath: arguments[0]).standardizedFileURL,
        output: URL(fileURLWithPath: arguments[1]).standardizedFileURL
    )
    var index = 2
    while index < arguments.count {
        let flag = arguments[index]
        guard index + 1 < arguments.count else {
            throw CommandError.usage("\(flag) requires a value\n\n\(usage)")
        }
        let value = arguments[index + 1]
        switch flag {
        case "--blur-radius":
            result.blurRadius = try nonNegative(value, flag: flag)
        case "--unsharp-radius":
            result.unsharpRadius = try nonNegative(value, flag: flag)
        case "--unsharp-intensity":
            result.unsharpIntensity = try nonNegative(value, flag: flag)
        default:
            throw CommandError.usage("unknown option: \(flag)\n\n\(usage)")
        }
        index += 2
    }
    return result
}

private func apply(
    name: String,
    image: CIImage,
    values: [String: Double]
) throws -> CIImage {
    guard let filter = CIFilter(name: name) else {
        throw CommandError.unavailableFilter(name)
    }
    filter.setValue(image, forKey: kCIInputImageKey)
    for (key, value) in values {
        filter.setValue(value, forKey: key)
    }
    guard let output = filter.outputImage else {
        throw CommandError.unavailableFilter(name)
    }
    return output
}

private func run(_ options: Options) throws {
    guard let source = CIImage(
        contentsOf: options.input,
        options: [.applyOrientationProperty: true]
    ) else {
        throw CommandError.unreadableImage(options.input)
    }
    let extent = source.extent.integral
    var filtered = source
    if options.blurRadius > 0 {
        filtered = try apply(
            name: "CIGaussianBlur",
            image: filtered,
            values: ["inputRadius": options.blurRadius]
        ).cropped(to: extent)
    }
    if options.unsharpRadius > 0 && options.unsharpIntensity > 0 {
        filtered = try apply(
            name: "CIUnsharpMask",
            image: filtered,
            values: [
                "inputRadius": options.unsharpRadius,
                "inputIntensity": options.unsharpIntensity,
            ]
        ).cropped(to: extent)
    }

    try FileManager.default.createDirectory(
        at: options.output.deletingLastPathComponent(),
        withIntermediateDirectories: true
    )
    guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB) else {
        throw CommandError.unavailableColorSpace
    }
    let context = CIContext(options: [
        .cacheIntermediates: false,
        .workingColorSpace: colorSpace,
        .outputColorSpace: colorSpace,
    ])
    try context.writePNGRepresentation(
        of: filtered,
        to: options.output,
        format: .RGBA8,
        colorSpace: colorSpace,
        options: [:]
    )
}

do {
    let options = try parse(Array(CommandLine.arguments.dropFirst()))
    try run(options)
} catch {
    FileHandle.standardError.write(Data("moire-filter: \(error)\n".utf8))
    Foundation.exit(EXIT_FAILURE)
}
