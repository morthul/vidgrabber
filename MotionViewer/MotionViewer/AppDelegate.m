//
//  AppDelegate.m
//  MotionViewer
//
//  Created by Dan Keen on 12/23/14.
//  Copyright (c) 2014 Dan Keen Research. All rights reserved.
//

#import "AppDelegate.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define ZONE_SIZE 3
#define ZONE_BLOCK_WIDTH 16
#define ZONE_BLOCK_HEIGHT 16

#define BLOCK_WIDTH (ZONE_SIZE * ZONE_BLOCK_WIDTH)
#define BLOCK_HEIGHT (ZONE_SIZE * ZONE_BLOCK_HEIGHT)

#define BLOCK_COUNT_HORIZONTAL  (int)(ceil((double)SCREEN_WIDTH / (double)BLOCK_WIDTH))
#define BLOCK_COUNT_VERTICAL    (int)(ceil((double)SCREEN_HEIGHT / (double)BLOCK_HEIGHT))

@protocol DKRImageViewDelegate
- (void)skipImage;
@end

@interface DKRImageView : NSView {
    NSImage *_image;
    unsigned int *_motionBlocks;
}
- (void)setImagePath:(NSString *)imagePath;
@property (nonatomic, weak) id<DKRImageViewDelegate> delegate;
@end

@implementation DKRImageView
- (id)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        NSLog(@"Block count horizontal: %d", BLOCK_COUNT_HORIZONTAL);
        NSLog(@"Block count vertical: %d", BLOCK_COUNT_VERTICAL);
//        NSLog(@"Initializing motionBlocks to hold %d blocks", BLOCK_COUNT_HORIZONTAL * BLOCK_COUNT_VERTICAL);
        _motionBlocks = calloc(sizeof(unsigned int), 1024);
    }
    return self;
}

- (void)dealloc {
    free(_motionBlocks);
}

- (void)_loadMotionData:(NSString *)imagePath {
    // Convert jpg to mot
    NSString *motionPath = [[imagePath substringToIndex:[imagePath length] - 3] stringByAppendingString:@"mot"];
    NSString *motionData = [[NSString alloc] initWithContentsOfFile:motionPath encoding:NSUTF8StringEncoding error:nil];
    NSArray *motionDataArray = [motionData componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];

//    NSLog(@"Motion count: %lu", (unsigned long)[motionDataArray count]);
    // First entry in array is "mdresult="
    // The array itself is a block of 32 by 32
    // However, we don't have an array of 32 by 32 - ours is 22 x 16
    // That means we need to ignore every entry from 23 => 32 in x and 17 => 32 in y.
    // Or we can populate them and just ignore them when drawing (or draw offscreen), which is what we choose to do.
    int trippedBlocks = 0;
    for (int i = 1; i < 1024; i++) {
        NSString *string = motionDataArray[i];
        string = [string stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        NSScanner *scanner = [NSScanner scannerWithString:string];
        unsigned int val;
        if ([scanner scanHexInt:&val]) {
            _motionBlocks[i-1] = val;
            if (val > 0) {
//                fprintf(stdout, "%d=>%d ", i-1, val);
                trippedBlocks++;
            }
        } else {
            _motionBlocks[i-1] = 0;
        }
    }

    double percentageOfBlocksTripped = (double)trippedBlocks / (double)(BLOCK_COUNT_HORIZONTAL * BLOCK_COUNT_VERTICAL);
    fprintf(stdout, "%s Tripped Blocks: %d (%.1f) => ", [imagePath UTF8String], trippedBlocks, percentageOfBlocksTripped * 100.0);

    // Skip if the count of blocks is greater than a threshold or too few
    if (percentageOfBlocksTripped > 0.2 || trippedBlocks <= 1) {
        fprintf(stdout, "SKIP\n");
        [_delegate skipImage];
    } else {
        fprintf(stdout, "DISP\n");
    }

    // Skip if the blocks are not contiguous
    // TODO
}

- (void)setImagePath:(NSString *)imagePath {
//    NSLog(@"Loading image %@", imagePath);
    _image = [[NSImage alloc] initWithContentsOfFile:imagePath];
    if (_image) {
        [self _loadMotionData:imagePath];
        [self setNeedsDisplay:YES];
    }
}

- (void)_drawBlocks {
    for (int i = 0; i < 1024; i++) {
        if (_motionBlocks[i] > 0) {
//            fprintf(stdout, "%d ", i);
            NSPoint topLeftPoint = NSMakePoint((i % 32) * BLOCK_WIDTH,
                                               (i / 32) * BLOCK_HEIGHT);
            CGRect fillRect = CGRectMake(topLeftPoint.x, SCREEN_HEIGHT - topLeftPoint.y - BLOCK_HEIGHT, BLOCK_WIDTH, BLOCK_HEIGHT);
            CGContextRef c = [[NSGraphicsContext currentContext] CGContext];
            CGContextSaveGState(c);
            CGContextSetRGBFillColor(c, 0.0, 1.0, 0.0, 1.0);
            CGContextFillRect(c, fillRect);
            CGContextRestoreGState(c);
        }
    }
//    fprintf(stdout, "\n");
}



- (void)drawRect:(NSRect)dirtyRect {
    [_image drawInRect:[self frame] fromRect:NSZeroRect operation:NSCompositeCopy fraction:1.0];
    [self _drawBlocks];
}
@end


@interface AppDelegate () <DKRImageViewDelegate> {
    DKRImageView *_imageView;
    NSArray *_files;
    int _fileIndex;
    NSString *_filePath;
}

@property (weak) IBOutlet NSWindow *window;

@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    _imageView = [[DKRImageView alloc] initWithFrame:[_window frame]];
    [_imageView setDelegate:self];
    _filePath = @"/tmp/11";
    _filePath = @"/Users/jobe/Desktop/vids/20141222/21";
    _files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:_filePath error:nil];
    [NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(_nextImage) userInfo:nil repeats:YES];
    [_window setContentView:_imageView];
}

- (void)_nextImage {
    NSString *file = _files[_fileIndex];
    if ([file hasSuffix:@"mot"]) {
        _fileIndex++;
        file = _files[_fileIndex];
    }
    _fileIndex += 2;
    [_imageView setImagePath:[_filePath stringByAppendingPathComponent:file]];
}

- (void)skipImage {
    [self _nextImage];
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // Insert code here to tear down your application
}

@end
