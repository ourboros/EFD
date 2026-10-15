#include "NetworkSyncWorkerApple.h"

#if defined(__APPLE__)
#import <Foundation/Foundation.h>

namespace efd {

bool PerformAppleHttpUpload(const std::string& url, const std::string& jsonBody, std::string& outResponse) {
    @autoreleasepool {
        NSString *urlStr = [NSString stringWithUTF8String:url.c_str()];
        NSURL *nsUrl = [NSURL URLWithString:urlStr];
        if (!nsUrl) {
            outResponse = "GAS_ERR: 無效的 Google Apps Script 端點 URL";
            return false;
        }

        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:nsUrl
                                                               cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                           timeoutInterval:25.0];
        [request setHTTPMethod:@"POST"];
        [request setValue:@"application/json; charset=utf-8" forHTTPHeaderField:@"Content-Type"];
        [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];

        NSData *postData = [NSData dataWithBytes:jsonBody.data() length:jsonBody.size()];
        [request setHTTPBody:postData];

        dispatch_semaphore_t sema = dispatch_semaphore_create(0);
        __block BOOL uploadOk = NO;
        __block NSString *responseLog = @"";

        NSURLSessionConfiguration *sessionConfig = [NSURLSessionConfiguration defaultSessionConfiguration];
        NSURLSession *session = [NSURLSession sessionWithConfiguration:sessionConfig];

        NSURLSessionDataTask *task = [session dataTaskWithRequest:request completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            if (error) {
                responseLog = [NSString stringWithFormat:@"GAS_ERR: %@", error.localizedDescription];
                uploadOk = NO;
            } else {
                NSHTTPURLResponse *httpResponse = (NSHTTPURLResponse *)response;
                NSString *respString = data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : @"";
                if (!respString && data) {
                    respString = [[NSString alloc] initWithData:data encoding:NSISOLatin1StringEncoding];
                }
                if (!respString) respString = @"";

                responseLog = [NSString stringWithFormat:@"GAS_RESP: HTTP %ld, %@", (long)httpResponse.statusCode, respString];

                // Google Apps Script 成功通常回傳 200/302 或包含 success: true / OK
                if (httpResponse.statusCode >= 200 && httpResponse.statusCode < 400) {
                    if ([respString rangeOfString:@"error" options:NSCaseInsensitiveSearch].location == NSNotFound ||
                        [respString rangeOfString:@"success" options:NSCaseInsensitiveSearch].location != NSNotFound) {
                        uploadOk = YES;
                    } else {
                        uploadOk = NO;
                    }
                } else {
                    uploadOk = NO;
                }
            }
            dispatch_semaphore_signal(sema);
        }];

        [task resume];
        // 最長等待 30 秒
        dispatch_semaphore_wait(sema, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(30 * NSEC_PER_SEC)));

        outResponse = [responseLog UTF8String];
        return uploadOk;
    }
}

} // namespace efd

#else

namespace efd {
bool PerformAppleHttpUpload(const std::string&, const std::string&, std::string& outResponse) {
    outResponse = "Apple HTTP upload is only available on Apple platforms";
    return false;
}
}

#endif
