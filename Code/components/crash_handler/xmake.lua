component("CrashHandler")
    -- sentry-native disabled: not compatible with MinGW cross-compilation
    -- Re-enable with add_packages("sentry-native") and add_defines("HAS_SENTRY") when sentry supports MinGW
