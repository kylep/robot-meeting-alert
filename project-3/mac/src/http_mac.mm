// fetch_url() for the iCal link source, on NSURLSession (Objective-C++, ARC).
// An ephemeral session: nothing about the link is cached on disk.
#import <Foundation/Foundation.h>

#include "ics_provider.hpp"

namespace rma {

bool fetch_url(const std::string& url, std::string* body, std::string* error) {
  @autoreleasepool {
    NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (u == nil) {
      *error = "that isn't a valid link";
      return false;
    }
    NSURLRequest* req = [NSURLRequest requestWithURL:u
                                         cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                     timeoutInterval:30];
    NSURLSession* session =
        [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration]];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block NSData* data = nil;
    __block NSInteger status = 0;
    __block NSInteger code = 0;
    __block BOOL failed = NO;
    NSURLSessionDataTask* task =
        [session dataTaskWithRequest:req
                   completionHandler:^(NSData* d, NSURLResponse* r, NSError* e) {
                     data = d;
                     if ([r isKindOfClass:[NSHTTPURLResponse class]]) status = ((NSHTTPURLResponse*)r).statusCode;
                     if (e != nil) {
                       failed = YES;
                       code = e.code;
                     }
                     dispatch_semaphore_signal(done);
                   }];
    [task resume];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    [session finishTasksAndInvalidate];
    // Error texts are built here, never from NSError, so the link can't leak into them.
    if (failed) {
      switch (code) {
        case NSURLErrorNotConnectedToInternet: *error = "no internet connection"; break;
        case NSURLErrorTimedOut: *error = "the server didn't answer"; break;
        case NSURLErrorCannotFindHost: *error = "server not found"; break;
        default: *error = "network error " + std::to_string(static_cast<long>(code)); break;
      }
      return false;
    }
    if (status != 200) {
      *error = "the server said HTTP " + std::to_string(static_cast<long>(status));
      return false;
    }
    if (data.length > 20 * 1024 * 1024) {
      *error = "the calendar is too big";
      return false;
    }
    body->assign(static_cast<const char*>(data.bytes), data.length);
    return true;
  }
}

}  // namespace rma
