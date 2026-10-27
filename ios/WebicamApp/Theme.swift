import SwiftUI

/// Graphite neutrals with one cold steel accent, same tokens as the Windows app.
enum Theme {
    static let base = Color(hex: 0x0F1012)
    static let raised = Color(hex: 0x1F2125)
    static let line = Color(hex: 0x25272B)
    static let text = Color(hex: 0xE6E7E9)
    static let muted = Color(hex: 0x8B9098)
    static let faint = Color(hex: 0x575B62)
    static let accent = Color(hex: 0xA9BACB)
    static let onAccent = Color(hex: 0x0F1012)

    /// Geist, bundled under Fonts/ (SIL OFL); scales with Dynamic Type.
    static func sans(_ size: CGFloat, _ weight: Font.Weight = .regular, relativeTo style: Font.TextStyle = .body) -> Font {
        let name: String
        switch weight {
        case .semibold, .bold, .heavy, .black: name = "Geist-SemiBold"
        case .medium: name = "Geist-Medium"
        default: name = "Geist-Regular"
        }
        return .custom(name, size: size, relativeTo: style)
    }

    static func mono(_ size: CGFloat, relativeTo style: Font.TextStyle = .body) -> Font {
        .custom("GeistMono-Medium", size: size, relativeTo: style)
    }
}

extension Color {
    init(hex: UInt32) {
        self.init(.sRGB,
                  red: Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255)
    }
}
