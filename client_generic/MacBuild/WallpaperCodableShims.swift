//  Codable shims matching WallpaperTypes encoding format.
//  参考 Phosphene 的实现（MIT 协议）。

import Foundation

// MARK: - Top-level view models

struct SettingsViewModels: Codable {
    var desktop: SettingsViewModel?
    var screenSaver: SettingsViewModel?
}

struct SettingsViewModel: Codable {
    var groups: [SettingsGroup]
    var refreshPolicy: RefreshPolicy
    var isModificationDisabled: Bool
}

struct SettingsGroup: Codable {
    var id: GroupID
    var items: [SettingsItem]
    var localizedName: String
    var disposability: Disposability
    var sortOrder: Int
    var sortID: GroupSortID?
    var allChoiceID: ChoiceID?
    var shouldHideItemLabels: Bool?
    var contextMenu: ContextMenu?
    var thumbnail: Data?
}

// MARK: - Item

struct SettingsItem: Codable {
    var id: ChoiceID
    var localizedName: String
    var thumbnail: Thumbnail
    var choice: ChoiceDescriptor
    var contentBadge: ContentBadge
    var showInTopLevel: Bool
    var sortOrder: Int
    var disposability: Disposability
}

// MARK: - Choice / ID types

struct ChoiceID: Codable {
    var id: String
    var descriptor: ChoiceIDDescriptor
}

struct ChoiceIDDescriptor: Codable {
    var provider: ChoiceProviderID
    var identifier: String
    var files: [URL]
    var configuration: Data
}

struct ChoiceDescriptor: Codable {
    var id: ChoiceID
    var provider: ChoiceProviderID
    var identifier: String
    var name: String?
    var localizedDescription: String
    var thumbnail: Thumbnail
    var isDownloaded: Bool
    var options: [WallpaperOption]
}

struct ChoiceProviderID: Codable {
    var rawValue: String

    func encode(to encoder: any Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(rawValue)
    }

    init(rawValue: String) { self.rawValue = rawValue }

    init(from decoder: any Decoder) throws {
        let container = try decoder.singleValueContainer()
        rawValue = try container.decode(String.self)
    }
}

struct WallpaperOption: Codable {}

// MARK: - Group / Sort ID

struct GroupID: Codable {
    var id: String
}

struct GroupSortID: Codable {
    var id: String
}

// MARK: - Enums

enum Disposability: Codable {
    case none, removable, purgeable

    private enum CodingKeys: String, CodingKey { case none, removable, purgeable }

    func encode(to encoder: any Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .none: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .none)
        case .removable: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .removable)
        case .purgeable: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .purgeable)
        }
    }

    init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        if container.contains(.none) { self = .none }
        else if container.contains(.removable) { self = .removable }
        else if container.contains(.purgeable) { self = .purgeable }
        else { self = .none }
    }
}

enum ContentBadge: Codable {
    case none, video, dynamic

    private enum CodingKeys: String, CodingKey { case none, video, dynamic }

    func encode(to encoder: any Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .none: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .none)
        case .video: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .video)
        case .dynamic: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .dynamic)
        }
    }

    init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        if container.contains(.none) { self = .none }
        else if container.contains(.video) { self = .video }
        else if container.contains(.dynamic) { self = .dynamic }
        else { self = .none }
    }
}

enum RefreshPolicy: Codable {
    case `default`

    private enum CodingKeys: String, CodingKey { case `default` }

    func encode(to encoder: any Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .default: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .default)
        }
    }

    init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        if container.contains(.default) { self = .default }
        else { self = .default }
    }
}

// MARK: - Thumbnail

enum Thumbnail: Codable {
    case image(url: URL)
    case customButton(CustomButton)

    private enum CodingKeys: String, CodingKey { case image, customButton }
    private enum ImageCodingKeys: String, CodingKey { case url }
    private enum CustomButtonCodingKeys: String, CodingKey { case _0 }

    func encode(to encoder: any Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case let .image(url):
            var nested = container.nestedContainer(keyedBy: ImageCodingKeys.self, forKey: .image)
            try nested.encode(url, forKey: .url)
        case let .customButton(button):
            var nested = container.nestedContainer(keyedBy: CustomButtonCodingKeys.self, forKey: .customButton)
            try nested.encode(button, forKey: ._0)
        }
    }

    init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        if container.contains(.image) {
            let nested = try container.nestedContainer(keyedBy: ImageCodingKeys.self, forKey: .image)
            self = .image(url: try nested.decode(URL.self, forKey: .url))
        } else if container.contains(.customButton) {
            let nested = try container.nestedContainer(keyedBy: CustomButtonCodingKeys.self, forKey: .customButton)
            self = .customButton(try nested.decode(CustomButton.self, forKey: ._0))
        } else {
            self = .image(url: URL(fileURLWithPath: "/"))
        }
    }
}

enum CustomButton: Codable {
    case addPhotoButton, addColorButton, shuffleColorsButton

    private enum CodingKeys: String, CodingKey { case addPhotoButton, addColorButton, shuffleColorsButton }

    func encode(to encoder: any Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .addPhotoButton: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .addPhotoButton)
        case .addColorButton: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .addColorButton)
        case .shuffleColorsButton: _ = container.nestedContainer(keyedBy: EmptyCodingKeys.self, forKey: .shuffleColorsButton)
        }
    }

    init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        if container.contains(.addPhotoButton) { self = .addPhotoButton }
        else if container.contains(.addColorButton) { self = .addColorButton }
        else if container.contains(.shuffleColorsButton) { self = .shuffleColorsButton }
        else { self = .addPhotoButton }
    }
}

// MARK: - Context Menu

struct ContextMenu: Codable {
    var items: [ContextMenuItem]
}

struct ContextMenuItem: Codable {
    var identifier: String
    var name: String
}

enum EmptyCodingKeys: CodingKey {}

// MARK: - ShimXPC encoding trick

/// NSObject wrapper that encodes SettingsViewModels using the same key as the real XPC type.
/// We archive this with NSKeyedArchiver, then on unarchive we remap the class name
/// to WallpaperSettingsViewModelsXPC (loaded via dlopen from WallpaperExtensionKit).
@objc(ShimViewModelsXPC)
class ShimViewModelsXPC: NSObject, NSSecureCoding {
    static let supportsSecureCoding = true
    let value: SettingsViewModels

    init(value: SettingsViewModels) {
        self.value = value
        super.init()
    }

    required init?(coder: NSCoder) {
        fatalError("ShimViewModelsXPC decode not needed")
    }

    func encode(with coder: NSCoder) {
        guard let archiver = coder as? NSKeyedArchiver else {
            extLog("[ShimXPC] encode error: coder is not NSKeyedArchiver")
            return
        }
        do {
            try archiver.encodeEncodable(value, forKey: "WallpaperSettingsViewModels")
        } catch {
            extLog("[ShimXPC] encode error: \(error)")
        }
    }
}

/// Archive via ShimViewModelsXPC, remap class name on unarchive to the real XPC type.
func remapToRealXPC(_ viewModels: SettingsViewModels) -> AnyObject? {
    let shimXPC = ShimViewModelsXPC(value: viewModels)

    let data: Data
    do {
        data = try NSKeyedArchiver.archivedData(withRootObject: shimXPC, requiringSecureCoding: false)
    } catch {
        extLog("[Remap] Archive failed: \(error)")
        return nil
    }

    guard let realClass = objc_getClass("WallpaperSettingsViewModelsXPC") as? AnyClass else {
        extLog("[Remap] WallpaperSettingsViewModelsXPC class not found")
        return nil
    }

    guard let unarchiver = try? NSKeyedUnarchiver(forReadingFrom: data) else {
        extLog("[Remap] Failed to create unarchiver")
        return nil
    }
    unarchiver.requiresSecureCoding = false
    unarchiver.decodingFailurePolicy = .setErrorAndReturn
    unarchiver.setClass(realClass, forClassName: "ShimViewModelsXPC")

    let result = unarchiver.decodeObject(forKey: NSKeyedArchiveRootObjectKey)
    if let error = unarchiver.error {
        extLog("[Remap] Unarchive error: \(error)")
    }
    unarchiver.finishDecoding()

    return result as AnyObject?
}
