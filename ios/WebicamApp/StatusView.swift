import SwiftUI
import UIKit

struct StatusView: View {
    let handler: ControlHandler
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var dimTask: Task<Void, Never>?
    /// Screen "off": nothing drawn on the black OLED and brightness at minimum.
    /// iOS stops the camera when the screen locks, so this is as dark as it gets.
    @State private var dimmed = false
    @State private var userBrightness: CGFloat?

    private var model: AppModel { handler.model }

    var body: some View {
        ZStack {
            (dimmed ? Color.black : Theme.base).ignoresSafeArea()
            if !dimmed {
                VStack(alignment: .leading, spacing: 0) {
                    Text("Webicam")
                        .font(Theme.sans(22, .semibold, relativeTo: .title2))
                        .foregroundStyle(Theme.text)
                    Spacer(minLength: 32)
                    status
                    Spacer(minLength: 32)
                    controls
                }
                .padding(.horizontal, 24)
                .padding(.vertical, 20)
                .transition(.opacity)
            }
        }
        .animation(reduceMotion ? nil : .easeOut(duration: 0.2), value: dimmed)
        .animation(reduceMotion ? nil : .easeOut(duration: 0.2), value: model.connected)
        .animation(reduceMotion ? nil : .easeOut(duration: 0.2), value: model.warning)
        .contentShape(Rectangle())
        .onTapGesture { wake() }
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true
            wake()
        }
        .onChange(of: scenePhase) { _, phase in
            if phase == .active {
                wake()
            } else {
                undim()  // iOS keeps our brightness after we leave; give the user theirs back
            }
        }
    }

    private var status: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 8) {
                // The dot is the real link state: steel while the PC is connected.
                Circle()
                    .fill(model.connected ? Theme.accent : Theme.faint)
                    .frame(width: 7, height: 7)
                Text(statusTitle)
                    .font(Theme.sans(15, .medium, relativeTo: .subheadline))
                    .foregroundStyle(model.connected ? Theme.text : Theme.muted)
            }
            .accessibilityElement(children: .combine)

            if model.connected {
                VStack(alignment: .leading, spacing: 6) {
                    Text(model.format)
                        .font(Theme.mono(34, relativeTo: .largeTitle))
                        .foregroundStyle(model.stopped ? Theme.muted : Theme.text)
                        .minimumScaleFactor(0.6)
                        .lineLimit(1)
                    Text(model.bitrate)
                        .font(Theme.mono(15, relativeTo: .subheadline))
                        .foregroundStyle(Theme.muted)
                }
            } else {
                Text("Подключите iPhone кабелем и запустите Webicam на компьютере")
                    .font(Theme.sans(26, .medium, relativeTo: .title))
                    .foregroundStyle(Theme.text)
                    .fixedSize(horizontal: false, vertical: true)
            }

            if let warning = model.warning {
                Text(warning)
                    .font(Theme.sans(15, relativeTo: .subheadline))
                    .foregroundStyle(Theme.text)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Theme.accent.opacity(0.10), in: RoundedRectangle(cornerRadius: 12))
                    .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(Theme.accent.opacity(0.35)))
                    .padding(.top, 6)
            }
        }
    }

    private var statusTitle: String {
        if !model.connected { return "Ожидание компьютера" }
        return model.stopped ? "Трансляция на паузе" : "Идёт трансляция"
    }

    private var controls: some View {
        VStack(spacing: 14) {
            Button {
                handler.toggleStopped()
            } label: {
                Text(model.stopped ? "Возобновить" : "Пауза")
                    .font(Theme.sans(17, .medium, relativeTo: .body))
                    .frame(maxWidth: .infinity, minHeight: 56)
            }
            .buttonStyle(PressableStyle(prominent: model.stopped))
            .sensoryFeedback(.impact(weight: .light), trigger: model.stopped)

            Text("Экран погаснет через 10 секунд. Коснитесь, чтобы включить.")
                .font(Theme.sans(13, relativeTo: .footnote))
                .foregroundStyle(Theme.muted)
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity)
        }
    }

    /// Shows the screen now and turns it "off" after 10 s without touches.
    private func wake() {
        undim()
        dimTask = Task { @MainActor in
            try? await Task.sleep(for: .seconds(10))
            guard !Task.isCancelled else { return }
            userBrightness = UIScreen.main.brightness
            UIScreen.main.brightness = 0
            dimmed = true
        }
    }

    private func undim() {
        dimTask?.cancel()
        dimTask = nil
        if dimmed, let userBrightness { UIScreen.main.brightness = userBrightness }
        dimmed = false
    }
}

/// Filled steel when it is the next thing to do, quiet graphite otherwise; sinks a little when pressed.
private struct PressableStyle: ButtonStyle {
    let prominent: Bool
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(prominent ? Theme.onAccent : Theme.text)
            .background(prominent ? Theme.accent : Theme.raised, in: RoundedRectangle(cornerRadius: 14))
            .opacity(configuration.isPressed ? 0.85 : 1)
            .scaleEffect(configuration.isPressed && !reduceMotion ? 0.98 : 1)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}
