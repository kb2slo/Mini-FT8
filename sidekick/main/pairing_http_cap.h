#pragma once

// Compile-time capacity for pairing_http's route-slot table.
//
// Policy still sits beside each handler at the pairing_http_register() call
// site (pairing_http.h). This header only answers "how many slots": each
// module owns a named count and _Static_assert's its local routes[] length
// against it. Add a route → bump that module's count (or the assert fails).
// Bump the count without adding → assert fails the other way when the array
// is shorter. The table size is the larger of the two httpd modes.

enum {
    PAIRING_ROUTES_DISCLOSURE = 1,  // /api/pairing-token

    PAIRING_ROUTES_WIFI_PROV_AP_CORE = 3,       // /  /rescan  /provision
    PAIRING_ROUTES_WIFI_PROV_STATION_CORE = 3,  // /  /update  /forget

    PAIRING_ROUTES_HOST_LINK = 7,
    PAIRING_ROUTES_WEB_BUNDLE = 3,
    PAIRING_ROUTES_BUNDLE_HOST = 2,
    PAIRING_ROUTES_WEB_FS_AP = 1,       // GET /*
    PAIRING_ROUTES_WEB_FS_STATION = 2,  // rehydrate + GET /*

    PAIRING_HTTP_MAX_ROUTES_AP = PAIRING_ROUTES_WIFI_PROV_AP_CORE +
                                 PAIRING_ROUTES_DISCLOSURE + PAIRING_ROUTES_WEB_FS_AP,

    PAIRING_HTTP_MAX_ROUTES_STATION =
        PAIRING_ROUTES_WIFI_PROV_STATION_CORE + PAIRING_ROUTES_DISCLOSURE +
        PAIRING_ROUTES_HOST_LINK + PAIRING_ROUTES_WEB_BUNDLE +
        PAIRING_ROUTES_BUNDLE_HOST + PAIRING_ROUTES_WEB_FS_STATION,

    PAIRING_HTTP_MAX_ROUTES =
        (PAIRING_HTTP_MAX_ROUTES_STATION > PAIRING_HTTP_MAX_ROUTES_AP)
            ? PAIRING_HTTP_MAX_ROUTES_STATION
            : PAIRING_HTTP_MAX_ROUTES_AP,
};
