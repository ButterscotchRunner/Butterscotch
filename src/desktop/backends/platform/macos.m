#include <AvailabilityMacros.h>
#include <AppKit/AppKit.h>

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030

static void show_error_box_nsalert(const char *message)
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

#if !__has_feature(objc_arc)
    [pool drain];
#endif

#if __has_feature(objc_arc)
    }
#endif
}

#else

static void show_error_box_nsrunalertpanel(const char *message)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSRunAlertPanel(
        @"Error",
        [NSString stringWithUTF8String:message],
        @"OK",
        nil,
        nil
    );

    [pool drain];
}

#endif

void show_error_box(const char *message)
{
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030
    show_error_box_nsalert(message);
#else
    show_error_box_nsrunalertpanel(message);
#endif
}