#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

@interface QRCodeScannerHelper : NSObject

+ (void)startQRCodeScanner;
+ (void)clearQRCodeResult;
+ (NSString *)getQRCodeFilePath;

@end
