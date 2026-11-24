typedef struct {
    const char *symbol;
    const char *value;
    fo_string *object;
} foundation_constant;

static foundation_constant foundation_constants[] = {
    {"_NSFileModificationDate", "NSFileModificationDate", NULL},
    {"_NSFilePosixPermissions", "NSFilePosixPermissions", NULL},
    {"_NSFileSize", "NSFileSize", NULL},
    {"_NSFileType", "NSFileType", NULL},
    {"_NSCalendarIdentifierGregorian", "gregorian", NULL},
    {"_NSLocalizedDescriptionKey", "NSLocalizedDescription", NULL},
    {"_NSLocalizedFailureReasonErrorKey", "NSLocalizedFailureReason", NULL},
    {"_NSLocalizedRecoverySuggestionErrorKey", "NSLocalizedRecoverySuggestion", NULL},
    {"_NSUnderlyingErrorKey", "NSUnderlyingError", NULL},
    {"_NSURLErrorKey", "NSURL", NULL},
    {"_NSURLErrorDomain", "NSURLErrorDomain", NULL},
    {"_NSURLErrorFailingURLErrorKey", "NSErrorFailingURLKey", NULL},
    {"_NSURLErrorFailingURLStringErrorKey", "NSErrorFailingURLStringKey", NULL},
    {"_NSURLErrorFailingURLPeerTrustErrorKey", "NSURLErrorFailingURLPeerTrustErrorKey", NULL},
    {"_NSURLAuthenticationMethodServerTrust", "NSURLAuthenticationMethodServerTrust", NULL},
    {"_NSURLVolumeNameKey", "NSURLVolumeNameKey", NULL},
    {"_NSURLFileAllocatedSizeKey", "NSURLFileAllocatedSizeKey", NULL},
    {"_NSURLTotalFileAllocatedSizeKey", "NSURLTotalFileAllocatedSizeKey", NULL},
    {"_NSURLIsRegularFileKey", "NSURLIsRegularFileKey", NULL},
    {"_NSURLIsDirectoryKey", "NSURLIsDirectoryKey", NULL},
    {"_NSURLFileSizeKey", "NSURLFileSizeKey", NULL},
    {"_NSURLContentModificationDateKey", "NSURLContentModificationDateKey", NULL},
    {"_NSKeyValueChangeNewKey", "new", NULL},
    {"_NSGenericException", "NSGenericException", NULL},
    {"_NSInternalInconsistencyException", "NSInternalInconsistencyException", NULL},
    {"_NSPOSIXErrorDomain", "NSPOSIXErrorDomain", NULL},
    {"_NSXMLParserErrorDomain", "NSXMLParserErrorDomain", NULL},
    {"_NSStreamFileCurrentOffsetKey", "NSStreamFileCurrentOffsetKey", NULL},
    {"_NSStreamDataWrittenToMemoryStreamKey", "NSStreamDataWrittenToMemoryStreamKey", NULL},
    {"_NSDefaultRunLoopMode", "kCFRunLoopDefaultMode", NULL},
    {"_NSRunLoopCommonModes", "kCFRunLoopCommonModes", NULL},
    {"_kCFBundleExecutableKey", "CFBundleExecutable", NULL},
    {"_kCFBundleIdentifierKey", "CFBundleIdentifier", NULL},
    {"_kCFBundleVersionKey", "CFBundleVersion", NULL},
    {"_kUTTagClassMIMEType", "public.mime-type", NULL},
    {"_kUTTagClassFilenameExtension", "public.filename-extension", NULL},
    {"_UIApplicationDidReceiveMemoryWarningNotification", "UIApplicationDidReceiveMemoryWarningNotification", NULL},
    {"_UIApplicationDidEnterBackgroundNotification", "UIApplicationDidEnterBackgroundNotification", NULL},
    {"_UIApplicationWillEnterForegroundNotification", "UIApplicationWillEnterForegroundNotification", NULL},
    {"_NSProgressFileURLKey", "NSProgressFileURLKey", NULL},
    {"_NSProgressFileCompletedCountKey", "NSProgressFileCompletedCountKey", NULL},
    {"_NSProgressFileTotalCountKey", "NSProgressFileTotalCountKey", NULL},
    {"_NSProgressKindFile", "NSProgressKindFile", NULL},
    {"_NSProgressFileOperationKindKey", "NSProgressFileOperationKindKey", NULL},
    {"_NSProgressFileOperationKindDownloading", "NSProgressFileOperationKindDownloading", NULL},
    {"_NSFontAttributeName", "NSFont", NULL},
    {"_NSParagraphStyleAttributeName", "NSParagraphStyle", NULL},
    {"_UIFontTextStyleBody", "UICTFontTextStyleBody", NULL},
    {"_UIFontTextStyleFootnote", "UICTFontTextStyleFootnote", NULL}
};

void *chm_foundation_constant(const char *symbol)
{
    static const int64_t unknown_transfer_size = -1;
    if (!strcmp(symbol, "_NSURLSessionTransferSizeUnknown")) return (void *)&unknown_transfer_size;
    if (!strcmp(symbol, "_kCFRunLoopCommonModes")) symbol = "_NSRunLoopCommonModes";
    for (size_t i = 0; i < sizeof(foundation_constants) / sizeof(*foundation_constants); i++) {
        foundation_constant *c = &foundation_constants[i];
        if (strcmp(symbol, c->symbol)) continue;
        if (!c->object) c->object = fo_string_new(c->value);
        return c->object ? &c->object : NULL;
    }
    return NULL;
}

static void constants_reset(void)
{
    for (size_t i = 0; i < sizeof(foundation_constants) / sizeof(*foundation_constants); i++) {
        fo_string *s = foundation_constants[i].object;
        if (!s) continue;
        free(s->bytes);
        free(s);
        foundation_constants[i].object = NULL;
    }
}
