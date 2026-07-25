#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030
#import <Cocoa/Cocoa.h>
#else
#import <AppKit/AppKit.h>
#endif

void show_error_box(const char *message)
{
#if __has_feature(objc_arc)
    @autoreleasepool {
#else
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
#endif

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030

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

#else

        NSRunAlertPanel(
            @"Error",
            [NSString stringWithUTF8String:message],
            @"OK",
            nil,
            nil
        );

#endif

#if !__has_feature(objc_arc)
    [pool drain];
#endif

#if __has_feature(objc_arc)
    }
#endif
}