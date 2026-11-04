import AppKit
import Foundation

/// One choice for Desktop and Idle so Settings lists us on both content types.
func buildSettingsViewModelsXPC() -> AnyObject? {
    let bundleID = Bundle.main.bundleIdentifier ?? "ai.infinidream.alpha.app.wallpaper"
    let choiceKey = "infinidream-metal"
    let thumbURL = URL(fileURLWithPath:
        "/System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/ToolbarCustomizeIcon.icns")

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
        name: "infinidream",
        localizedDescription: "infinidream live wallpaper",
        thumbnail: thumb,
        isDownloaded: true,
        options: []
    )
    let item = SettingsItem(
        id: choiceID,
        localizedName: "infinidream",
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
        localizedName: "infinidream",
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
    return remapToRealXPC(SettingsViewModels(desktop: viewModel, screenSaver: viewModel))
}

func makeEmptyGroupsResponse() -> AnyObject? {
    let empty = SettingsViewModel(groups: [], refreshPolicy: .default, isModificationDisabled: false)
    return remapToRealXPC(SettingsViewModels(desktop: empty, screenSaver: empty))
}
