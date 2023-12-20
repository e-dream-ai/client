//
//  MBEFontAtlas.h
//  TextRendering
//
//  Created by Warren Moore on 2/6/15.
//  Copyright (c) 2015 Metal By Example. All rights reserved.
//

#import <AppKit/AppKit.h>

#define USE_LIMITED_CHARSET 1
#define MAX_UNICHAR_INDEX 256

@interface MBEGlyphDescriptor : NSObject <NSSecureCoding>
@property(nonatomic, assign) CGGlyph glyphIndex;
@property(nonatomic, assign) CGPoint topLeftTexCoord;
@property(nonatomic, assign) CGPoint bottomRightTexCoord;
@end

@interface MBEFontAtlas : NSObject <NSSecureCoding>

@property(nonatomic, readonly) NSFont* parentFont;
@property(nonatomic, readonly) CGFloat fontPointSize;
@property(nonatomic, readonly) CGFloat spread;
@property(nonatomic, readonly) size_t textureSize;
@property(weak, nonatomic, readonly) NSArray* glyphDescriptors;
@property(weak, nonatomic, readonly) NSData* textureData;

/// Create a signed-distance field based font atlas with the specified
/// dimensions. The supplied font will be resized to fit all available glyphs in
/// the texture.
- (instancetype)initWithFont:(NSFont*)font textureSize:(size_t)textureSize;

@end
