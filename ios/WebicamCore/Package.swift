// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "WebicamCore",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [.library(name: "WebicamCore", targets: ["WebicamCore"])],
    targets: [
        .target(name: "WebicamCore"),
        .testTarget(name: "WebicamCoreTests", dependencies: ["WebicamCore"]),
    ]
)
