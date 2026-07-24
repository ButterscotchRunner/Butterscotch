#import <Cocoa/Cocoa.h>

void show_error_box(const char *message)
{
#if __has_feature(objc_arc)
    @autoreleasepool {
#else
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
#endif

        NSAlert *alert = [[NSAlert alloc] init];

        [alert setMessageText:@"Error"];
        [alert setInformativeText:[NSString stringWithUTF8String:message]];
        [alert addButtonWithTitle:@"OK"];

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
        [alert setAlertStyle:NSAlertStyleCritical];
#else
        [alert setAlertStyle:NSCriticalAlertStyle];
#endif

        [alert runModal];

#if !__has_feature(objc_arc)
        [alert release];
#endif

#if __has_feature(objc_arc)
    }
#else
    [pool drain];
#endif
}