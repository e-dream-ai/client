//
//  MBEMesh.h
//  TextRendering
//
//  Created by Warren Moore on 11/10/14.
//  Copyright (c) 2014 Metal By Example. All rights reserved.
//

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>

@interface MBEMesh : NSObject

@property(nonatomic, readonly) id<MTLBuffer> vertexBuffer;
@property(nonatomic, readonly) id<MTLBuffer> indexBuffer;

@end
