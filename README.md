# QRCodeScan-for-IOS
QRCodeScannerHelper.h  
```
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

@interface QRCodeScannerHelper : NSObject

+ (void)startQRCodeScanner;
+ (void)clearQRCodeResult;
+ (NSString *)getQRCodeFilePath;

@end

```
QRCodeScannerHelper.m  
此版本開啟掃描時, 超過2秒, 會無法掃描  
```
#import "QRCodeScannerHelper.h"
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <Vision/Vision.h>

@interface QRCodeScannerHelper () <AVCaptureVideoDataOutputSampleBufferDelegate>

@property (nonatomic, strong) AVCaptureSession *captureSession;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *previewLayer;
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;
@property (nonatomic, strong) UIViewController *scannerViewController;
@property (nonatomic, assign) BOOL hasScanned;
@property (nonatomic) dispatch_queue_t videoQueue;
@property (nonatomic, assign) BOOL isProcessingFrame;
@property (nonatomic, assign) NSInteger frameCount;
@property (nonatomic, strong) dispatch_queue_t visionQueue;



@end

@implementation QRCodeScannerHelper

+ (QRCodeScannerHelper *)sharedHelper {
    static QRCodeScannerHelper *helper = nil;
    static dispatch_once_t onceToken;

    dispatch_once(&onceToken, ^{
        helper = [[QRCodeScannerHelper alloc] init];
    });

    return helper;
}

+ (void)startQRCodeScanner {
    NSLog(@"📷 startQRCodeScanner called");

    dispatch_async(dispatch_get_main_queue(), ^{
        [[QRCodeScannerHelper sharedHelper] startScanner];
    });
}

+ (NSString *)getQRCodeFilePath {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Documents/qrcode.txt"];
}

+ (void)clearQRCodeResult {
    NSString *filePath = [QRCodeScannerHelper getQRCodeFilePath];

    if ([[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
        [[NSFileManager defaultManager] removeItemAtPath:filePath error:nil];
        NSLog(@"🗑 old QRCode file removed");
    }
}

- (void)startScanner {
    NSLog(@"📷 startScanner called");

    self.hasScanned = NO;

    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

    NSLog(@"📷 camera authorization status: %ld", (long)status);

    if (status == AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {

            dispatch_async(dispatch_get_main_queue(), ^{
                if (granted) {
                    NSLog(@"✅ camera permission granted");
                    [self setupScanner];
                } else {
                    NSLog(@"❌ camera permission denied");
                    [self saveQRCodeText:@"CAMERA_PERMISSION_DENIED"];
                }
            });
        }];

        return;
    }

    if (status != AVAuthorizationStatusAuthorized) {
        NSLog(@"❌ camera not authorized");
        [self saveQRCodeText:@"CAMERA_PERMISSION_DENIED"];
        return;
    }

    [self setupScanner];
}

- (void)setupScanner {
    NSLog(@"📷 setupScanner called");

    self.visionQueue = dispatch_queue_create("qrcode.vision.queue", DISPATCH_QUEUE_SERIAL);
    self.captureSession = [[AVCaptureSession alloc] init];
    self.captureSession.sessionPreset = AVCaptureSessionPresetHigh;

    AVCaptureDevice *videoDevice =
        [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];

    if (!videoDevice) {
        NSLog(@"❌ no camera device found");
        [self saveQRCodeText:@"CAMERA_DEVICE_NOT_FOUND"];
        return;
    }

    NSError *inputError = nil;

    AVCaptureDeviceInput *videoInput =
        [AVCaptureDeviceInput deviceInputWithDevice:videoDevice
                                              error:&inputError];

    if (inputError || !videoInput) {
        NSLog(@"❌ camera input error: %@", inputError);
        [self saveQRCodeText:@"CAMERA_INPUT_ERROR"];
        return;
    }

    if ([self.captureSession canAddInput:videoInput]) {
        [self.captureSession addInput:videoInput];
        NSLog(@"✅ camera input added");
    } else {
        NSLog(@"❌ cannot add camera input");
        [self saveQRCodeText:@"CAMERA_INPUT_ADD_FAILED"];
        return;
    }

    self.videoOutput = [[AVCaptureVideoDataOutput alloc] init];

    self.videoOutput.videoSettings =
    @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey:
            @(kCVPixelFormatType_32BGRA)
    };

    self.videoQueue =
        dispatch_queue_create("qrcode.video.queue", DISPATCH_QUEUE_SERIAL);

    [self.videoOutput setSampleBufferDelegate:self
                                        queue:self.videoQueue];

    self.videoOutput.alwaysDiscardsLateVideoFrames = YES;

    if ([self.captureSession canAddOutput:self.videoOutput]) {
        [self.captureSession addOutput:self.videoOutput];
        NSLog(@"✅ video output added");
    } else {
        NSLog(@"❌ cannot add video output");
        [self saveQRCodeText:@"VIDEO_OUTPUT_ADD_FAILED"];
        return;
    }

    self.scannerViewController = [[UIViewController alloc] init];
    self.scannerViewController.view.backgroundColor = [UIColor blackColor];

    self.previewLayer =
        [AVCaptureVideoPreviewLayer layerWithSession:self.captureSession];

    self.previewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
    self.previewLayer.frame = [UIScreen mainScreen].bounds;

    [self.scannerViewController.view.layer addSublayer:self.previewLayer];

    UIButton *closeButton = [UIButton buttonWithType:UIButtonTypeSystem];
    closeButton.frame = CGRectMake(20, 50, 100, 44);
    [closeButton setTitle:@"Close" forState:UIControlStateNormal];
    [closeButton setTitleColor:[UIColor whiteColor]
                       forState:UIControlStateNormal];
    closeButton.titleLabel.font = [UIFont boldSystemFontOfSize:18];

    [closeButton addTarget:self
                    action:@selector(closeScanner)
          forControlEvents:UIControlEventTouchUpInside];

    [self.scannerViewController.view addSubview:closeButton];

    UIViewController *rootVC = [self topViewController];

    if (!rootVC) {
        NSLog(@"❌ root view controller not found");
        [self saveQRCodeText:@"ROOT_VIEW_CONTROLLER_NOT_FOUND"];
        return;
    }

    NSLog(@"📷 presenting scanner");

    [rootVC presentViewController:self.scannerViewController
                         animated:YES
                       completion:^{
        NSLog(@"📷 scanner presented");

        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            [self.captureSession startRunning];

            if (self.captureSession.isRunning) {
                NSLog(@"✅ captureSession started");
            } else {
                NSLog(@"❌ captureSession not running");
            }
        });
    }];
}

- (UIViewController *)topViewController {
    UIWindow *keyWindow = nil;

    if (@available(iOS 13.0, *)) {
        for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
            if (scene.activationState == UISceneActivationStateForegroundActive &&
                [scene isKindOfClass:[UIWindowScene class]]) {

                UIWindowScene *windowScene = (UIWindowScene *)scene;

                for (UIWindow *window in windowScene.windows) {
                    if (window.isKeyWindow) {
                        keyWindow = window;
                        break;
                    }
                }
            }
        }
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        keyWindow = [UIApplication sharedApplication].keyWindow;
#pragma clang diagnostic pop
    }

    UIViewController *rootVC = keyWindow.rootViewController;

    while (rootVC.presentedViewController) {
        rootVC = rootVC.presentedViewController;
    }

    return rootVC;
}

- (void)captureOutput:(AVCaptureOutput *)output
 didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
        fromConnection:(AVCaptureConnection *)connection {

    if (self.hasScanned || self.isProcessingFrame) {
        return;
    }

    self.frameCount++;

    // 每 10 幀掃一次，避免太重
    if (self.frameCount % 10 != 0) {
        return;
    }

    self.isProcessingFrame = YES;

    CMSampleBufferRef bufferCopy = sampleBuffer;
    CFRetain(bufferCopy);

    dispatch_async(self.visionQueue, ^{

        @autoreleasepool {

            VNDetectBarcodesRequest *request =
            [[VNDetectBarcodesRequest alloc] initWithCompletionHandler:
             ^(VNRequest *request, NSError *error) {

                if (error) {
                    NSLog(@"❌ Vision barcode error: %@", error);
                    self.isProcessingFrame = NO;
                    return;
                }

                NSArray<VNBarcodeObservation *> *results =
                    (NSArray<VNBarcodeObservation *> *)request.results;

                for (VNBarcodeObservation *observation in results) {

                    NSString *payload = observation.payloadStringValue;

                    if (payload && payload.length > 0) {

                        self.hasScanned = YES;

                        NSLog(@"✅ QRCode detected: %@", payload);

                        dispatch_async(dispatch_get_main_queue(), ^{
                            [self saveQRCodeText:payload];
                            [self closeScanner];
                        });

                        self.isProcessingFrame = NO;
                        CFRelease(bufferCopy);
                        return;
                    }
                }

                self.isProcessingFrame = NO;
            }];

            VNImageRequestHandler *handler =
            [[VNImageRequestHandler alloc] initWithCMSampleBuffer:bufferCopy
                                                      orientation:kCGImagePropertyOrientationRight
                                                          options:@{}];

            NSError *visionError = nil;
            [handler performRequests:@[request] error:&visionError];

            if (visionError) {
                NSLog(@"❌ Vision perform error: %@", visionError);
                self.isProcessingFrame = NO;
            }

            CFRelease(bufferCopy);
        }
    });
}


- (void)saveQRCodeText:(NSString *)text {
    NSString *filePath = [QRCodeScannerHelper getQRCodeFilePath];

    NSError *error = nil;

    BOOL success =
        [text writeToFile:filePath
               atomically:YES
                 encoding:NSUTF8StringEncoding
                    error:&error];

    if (success) {
        NSLog(@"✅ QRCode saved to: %@", filePath);
        NSLog(@"✅ QRCode saved text: %@", text);
    } else {
        NSLog(@"❌ Save QRCode failed: %@", error);
    }
}

- (void)closeScanner {
    NSLog(@"📷 closeScanner called");

    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.captureSession && self.captureSession.isRunning) {
            [self.captureSession stopRunning];
            NSLog(@"📷 captureSession stopped");
        }

        if (self.scannerViewController) {
            [self.scannerViewController dismissViewControllerAnimated:YES
                                                            completion:^{
                NSLog(@"📷 scanner dismissed");
            }];
        }

        self.previewLayer = nil;
        self.videoOutput = nil;
        self.captureSession = nil;
        self.scannerViewController = nil;
        self.videoQueue = nil;
    });
}

@end

```
info.plist
```
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleDisplayName</key>
	<string>${PRODUCT_NAME}</string>
	<key>CFBundleExecutable</key>
	<string>${EXECUTABLE_NAME}</string>
	<key>CFBundleIcons</key>
	<dict/>
	<key>CFBundleIdentifier</key>
	<string>$(PRODUCT_BUNDLE_IDENTIFIER)</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>${PRODUCT_NAME}</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>1.1</string>
	<key>CFBundleVersion</key>
	<string>1.1</string>
	<key>ITSAppUsesNonExemptEncryption</key>
	<false/>
	<key>LSRequiresIPhoneOS</key>
	<true/>
	<key>NSCameraUsageDescription</key>
	<string>我們需要使用相機來讓使用者拍攝熱水器圖片，以便維修或紀錄。</string>
    <key>NSCameraUsageDescription</key>
    <string>需要使用相機掃描 QRCode</string>
	<key>UIBackgroundModes</key>
	<array>
		<string>remote-notification</string>
	</array>
	<key>UILaunchStoryboardName</key>
	<string>Launch Screen</string>
	<key>UIStatusBarHidden</key>
	<true/>
	<key>UISupportedInterfaceOrientations</key>
	<array>
		<string>UIInterfaceOrientationLandscapeLeft</string>
		<string>UIInterfaceOrientationLandscapeRight</string>
		<string>UIInterfaceOrientationPortrait</string>
		<string>UIInterfaceOrientationPortraitUpsideDown</string>
	</array>
	<key>UISupportedInterfaceOrientations~ipad</key>
	<array>
		<string>UIInterfaceOrientationLandscapeLeft</string>
		<string>UIInterfaceOrientationLandscapeRight</string>
		<string>UIInterfaceOrientationPortrait</string>
		<string>UIInterfaceOrientationPortraitUpsideDown</string>
	</array>
</dict>
</plist>

```

