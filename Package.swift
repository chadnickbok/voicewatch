// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "CleanCam",
    platforms: [
        .macOS(.v14)
    ],
    products: [
        .executable(name: "CleanCam", targets: ["CleanCam"]),
        .executable(name: "moire-filter", targets: ["MoireFilter"])
    ],
    targets: [
        .target(
            name: "CUVCControl",
            publicHeadersPath: "include",
            linkerSettings: [
                .linkedFramework("IOKit"),
                .linkedFramework("CoreFoundation")
            ]
        ),
        .executableTarget(
            name: "CleanCam",
            dependencies: ["CUVCControl"],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("CoreImage"),
                .linkedFramework("ImageIO")
            ]
        ),
        .executableTarget(
            name: "MoireFilter",
            linkerSettings: [
                .linkedFramework("CoreImage"),
                .linkedFramework("ImageIO")
            ]
        )
    ],
    swiftLanguageModes: [.v5]
)
