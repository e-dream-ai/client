import AppKit
import Foundation

/// One choice for Desktop and Idle so Settings lists us on both content types.
func buildSettingsViewModelsXPC() -> AnyObject? {
    let bundleID = Bundle.main.bundleIdentifier ?? "ai.infinidream.alpha.app.wallpaper"
    let choiceKey = "infinidream-metal"
    // Group / tile label in System Settings → Wallpaper / Screen Saver.
    let displayName = "Infinidream"
    let thumbURL = settingsThumbnailURL()

    let choiceID = ChoiceID(
        id: choiceKey,
        descriptor: ChoiceIDDescriptor(
            provider: ChoiceProviderID(rawValue: bundleID),
            identifier: choiceKey,
            files: [],
            configuration: Data()
        )
    )
    let thumb: Thumbnail = .image(url: thumbURL)
    let descriptor = ChoiceDescriptor(
        id: choiceID,
        provider: ChoiceProviderID(rawValue: bundleID),
        identifier: choiceKey,
        name: displayName,
        localizedDescription: "\(displayName) live wallpaper",
        thumbnail: thumb,
        isDownloaded: true,
        options: []
    )
    let item = SettingsItem(
        id: choiceID,
        localizedName: displayName,
        thumbnail: thumb,
        choice: descriptor,
        contentBadge: .none,
        showInTopLevel: true,
        sortOrder: 0,
        disposability: .none
    )
    let group = SettingsGroup(
        id: GroupID(id: "infinidream-wallpaper"),
        items: [item],
        localizedName: displayName,
        disposability: .none,
        sortOrder: 0,
        sortID: GroupSortID(id: "com.apple.wallpaper.aerials"),
        allChoiceID: nil,
        shouldHideItemLabels: false,
        contextMenu: nil,
        thumbnail: nil
    )
    let viewModel = SettingsViewModel(
        groups: [group],
        refreshPolicy: .default,
        isModificationDisabled: false
    )
    extLog("settings view models name=\(displayName) thumb=\(thumbURL.path)")
    return remapToRealXPC(SettingsViewModels(desktop: viewModel, screenSaver: viewModel))
}

func makeEmptyGroupsResponse() -> AnyObject? {
    let empty = SettingsViewModel(groups: [], refreshPolicy: .default, isModificationDisabled: false)
    return remapToRealXPC(SettingsViewModels(desktop: empty, screenSaver: empty))
}

/// System Settings loads thumbnail URLs from WallpaperAgent — a path inside the
/// .appex bundle is not reliably readable there. Export the app icon to a PNG
/// in the extension container (same pattern as Phosphene).
private func settingsThumbnailURL() -> URL {
    let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
        ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Documents")
    let dest = docs.appendingPathComponent("settings-thumbnail.png")

    if let size = try? dest.resourceValues(forKeys: [.fileSizeKey]).fileSize, size > 0 {
        return dest
    }

    let image = loadAppIconImage()
    if let image, let png = pngData(from: image) {
        do {
            try FileManager.default.createDirectory(at: docs, withIntermediateDirectories: true)
            try png.write(to: dest, options: .atomic)
            extLog("settings thumbnail wrote \(dest.path) bytes=\(png.count)")
            return dest
        } catch {
            extLog("settings thumbnail write failed: \(error)")
        }
    } else {
        extLog("settings thumbnail: could not load app icon")
    }

    // Last resort — still better than a missing tile; System Settings can read this.
    return URL(fileURLWithPath:
        "/System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/GenericApplicationIcon.icns")
}

private func loadAppIconImage() -> NSImage? {
    if let image = Bundle.main.image(forResource: "infinidream") { return image }
    if let url = Bundle.main.url(forResource: "infinidream", withExtension: "icns"),
       let image = NSImage(contentsOf: url) { return image }
    if let url = Bundle.main.url(forResource: "logo", withExtension: "png"),
       let image = NSImage(contentsOf: url) { return image }
    return nil
}

private func pngData(from image: NSImage) -> Data? {
    let maxEdge: CGFloat = 512
    let srcSize = image.size
    let scale = min(1.0, min(maxEdge / max(srcSize.width, 1), maxEdge / max(srcSize.height, 1)))
    let size = NSSize(width: floor(srcSize.width * scale), height: floor(srcSize.height * scale))
    let output = NSImage(size: size)
    output.lockFocus()
    NSGraphicsContext.current?.imageInterpolation = .high
    image.draw(in: NSRect(origin: .zero, size: size),
               from: NSRect(origin: .zero, size: srcSize),
               operation: .copy,
               fraction: 1.0)
    output.unlockFocus()
    guard let tiff = output.tiffRepresentation,
          let rep = NSBitmapImageRep(data: tiff) else { return nil }
    return rep.representation(using: .png, properties: [:])
}
