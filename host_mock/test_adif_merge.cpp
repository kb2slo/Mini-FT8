/*
 * ADIF copy-to-SD merge: union onto the archive, never drop a unique QSO,
 * never duplicate station+call+date+time_on.
 */
#include <cstdio>
#include <string>
#include <vector>

#include "adif.h"

static int g_fails = 0;

static void fail(const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    ++g_fails;
}

static void expect_true(bool cond, const char* msg) {
    if (!cond) {
        fail(msg);
    }
}

static void expect_status(AdifMergeStatus got, AdifMergeStatus want, const char* msg) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, (int)got, (int)want);
        ++g_fails;
    }
}

static std::string rec(const char* call,
                       const char* grid,
                       const char* date,
                       const char* time_on,
                       const char* station,
                       const char* comment,
                       const char* rst_sent = nullptr,
                       const char* rst_rcvd = nullptr) {
    std::string line;
    auto field = [&](const char* name, const char* value) {
        const std::string v = value ? value : "";
        line += "<";
        line += name;
        line += ":";
        line += std::to_string(v.size());
        line += ">";
        line += v;
        line += " ";
    };
    field("call", call);
    field("gridsquare", grid);
    field("mode", "FT8");
    field("qso_date", date);
    field("time_on", time_on);
    field("freq", "14.074");
    field("station_callsign", station);
    field("my_gridsquare", "FN30");
    if (rst_sent) {
        field("rst_sent", rst_sent);
    }
    if (rst_rcvd) {
        field("rst_rcvd", rst_rcvd);
    }
    field("comment", comment);
    line += "<eor>";
    return line;
}

static std::string adi_file(const std::vector<std::string>& records) {
    std::string out = "ADIF EXPORT\n<eoh>\n";
    for (const std::string& r : records) {
        out += r;
        out += "\n";
    }
    return out;
}

static std::vector<AdifRecord> must_parse(const std::string& content, const char* msg) {
    std::vector<AdifRecord> records;
    if (!adif_parse(content, records)) {
        fail(msg);
    }
    return records;
}

static bool has_key_call_time(const std::vector<AdifRecord>& records,
                              const char* call,
                              const char* time_on) {
    for (const AdifRecord& r : records) {
        if (r.key.find(call) != std::string::npos &&
            r.key.size() >= 6 &&
            r.key.compare(r.key.size() - 6, 6, time_on) == 0) {
            return true;
        }
    }
    return false;
}

static bool comment_for_call_time(const std::string& file,
                                  const char* call,
                                  const char* time_on,
                                  const char* comment) {
    std::vector<AdifRecord> records;
    if (!adif_parse(file, records)) {
        return false;
    }
    const std::string needle_call = std::string(call);
    for (const AdifRecord& r : records) {
        if (r.key.find(needle_call) == std::string::npos) {
            continue;
        }
        if (r.key.size() < 6 || r.key.compare(r.key.size() - 6, 6, time_on) != 0) {
            continue;
        }
        return r.text.find(comment) != std::string::npos;
    }
    return false;
}

int main() {
    expect_true(adif_is_adi_filename("20260817.adi"), "daily adi");
    expect_true(adif_is_adi_filename("/storage/20260817.ADI"), "path + upper ext");
    expect_true(!adif_is_adi_filename("Station.txt"), "station is not adi");
    expect_true(!adif_is_adi_filename("RT260817.txt"), "rt log is not adi");
    expect_true(!adif_is_adi_filename("20260817.txt"), "legacy txt is not adi");

    std::vector<AdifRecord> parsed;
    expect_true(adif_parse("", parsed) && parsed.empty(), "empty file");
    expect_true(adif_parse("ADIF EXPORT\n<eoh>\n", parsed) && parsed.empty(),
                "header only");
    expect_true(adif_parse("\xEF\xBB\xBF" "ADIF EXPORT\n<eoh>\n", parsed) && parsed.empty(),
                "utf8 bom header only");

    const std::string k1 =
        rec("K1ABC", "FN42", "20260817", "153000", "KB2SLO", "first");
    const std::string k2 =
        rec("W1AW", "FN31", "20260817", "153015", "KB2SLO", "second");
    const std::string k1_later =
        rec("K1ABC", "FN42", "20260817", "153500", "KB2SLO", "later");
    const std::string k1_dup_comment =
        rec("K1ABC", "FN42", "20260817", "153000", "KB2SLO", "edited-on-sd");
    const std::string other_station =
        rec("K1ABC", "FN42", "20260817", "153000", "N0CALL", "other-op");

    const std::string incoming = adi_file({k1, k2});
    parsed = must_parse(incoming, "parse mini-ft8 incoming");
    expect_true(parsed.size() == 2, "two incoming records");

    std::string out;
    expect_status(adif_merge_export("", incoming, out), AdifMergeStatus::OK,
                  "empty archive copies incoming");
    parsed = must_parse(out, "parse copy result");
    expect_true(parsed.size() == 2, "copied both records");

    expect_status(adif_merge_export(incoming, "", out), AdifMergeStatus::OK,
                  "empty incoming keeps archive");
    parsed = must_parse(out, "parse archive-only result");
    expect_true(parsed.size() == 2, "archive-only both records");

    const std::string archive = adi_file({k1, k2});
    const std::string grown = adi_file({k1, k2, k1_later});
    expect_status(adif_merge_export(archive, grown, out), AdifMergeStatus::OK,
                  "typical re-copy appends new qso");
    parsed = must_parse(out, "parse grown merge");
    expect_true(parsed.size() == 3, "union has three");
    expect_true(has_key_call_time(parsed, "K1ABC", "153500"), "kept later k1abc");

    const std::string sd_only = adi_file({k1, k2});
    const std::string internal_short = adi_file({k2});
    expect_status(adif_merge_export(sd_only, internal_short, out), AdifMergeStatus::OK,
                  "internal subset must not drop sd qsos");
    parsed = must_parse(out, "parse subset merge");
    expect_true(parsed.size() == 2, "kept sd-only k1abc");
    expect_true(has_key_call_time(parsed, "K1ABC", "153000"), "k1abc survived");

    const std::string sd_edited = adi_file({k1_dup_comment});
    const std::string flash_orig = adi_file({k1, k1_later});
    expect_status(adif_merge_export(sd_edited, flash_orig, out), AdifMergeStatus::OK,
                  "duplicate key keeps archive comment");
    parsed = must_parse(out, "parse conflict merge");
    expect_true(parsed.size() == 2, "dup dropped, later kept");
    expect_true(comment_for_call_time(out, "K1ABC", "153000", "edited-on-sd"),
                "archive wins on same key");
    expect_true(!comment_for_call_time(out, "K1ABC", "153000", "first"),
                "incoming dup comment dropped");

    expect_status(adif_merge_export(adi_file({k1}), adi_file({other_station}), out),
                  AdifMergeStatus::OK, "same dx/time different station");
    parsed = must_parse(out, "parse two-station merge");
    expect_true(parsed.size() == 2, "two operators are distinct qsOs");

    expect_status(adif_merge_export(adi_file({k1}), adi_file({k1_later}), out),
                  AdifMergeStatus::OK, "same call five minutes later");
    parsed = must_parse(out, "parse 10min-not-applied");
    expect_true(parsed.size() == 2, "merge does not use logger 10-min window");

    const std::string crlf = "ADIF EXPORT\r\n<EOH>\r\n" + k1 + "\r\n";
    expect_status(adif_merge_export(crlf, "", out), AdifMergeStatus::OK, "crlf + upper tags");
    parsed = must_parse(out, "parse crlf result");
    expect_true(parsed.size() == 1, "one crlf record");

    const std::string multiline =
        "ADIF EXPORT\n<eoh>\n<call:5>K1ABC\n<gridsquare:4>FN42 <mode:3>FT8"
        "<qso_date:8>20260817 <time_on:6>160000 <station_callsign:6>KB2SLO <eor>\n";
    expect_status(adif_merge_export(multiline, incoming, out), AdifMergeStatus::OK,
                  "multiline record");
    parsed = must_parse(out, "parse multiline merge");
    expect_true(parsed.size() == 3, "multiline plus two incoming");

    const std::string lower_call =
        rec("k1abc", "FN42", "20260817", "153000", "kb2slo", "lower");
    expect_status(adif_merge_export(adi_file({k1}), adi_file({lower_call}), out),
                  AdifMergeStatus::OK, "case-insensitive call key");
    parsed = must_parse(out, "parse case merge");
    expect_true(parsed.size() == 1, "k1abc and K1ABC are one qso");

    std::string first;
    std::string second;
    expect_status(adif_merge_export(archive, grown, first), AdifMergeStatus::OK, "idempotent 1");
    expect_status(adif_merge_export(first, grown, second), AdifMergeStatus::OK, "idempotent 2");
    expect_true(first == second, "merge is idempotent");

    const std::string before_fail = "keep-me";
    out = before_fail;
    expect_status(adif_merge_export("not adif at all", incoming, out),
                  AdifMergeStatus::PARSE_ARCHIVE, "garbage archive");
    expect_true(out.empty(), "failed merge clears out");

    out = before_fail;
    expect_status(adif_merge_export(archive, "hello world", out),
                  AdifMergeStatus::PARSE_INCOMING, "garbage incoming");
    expect_true(out.empty(), "failed incoming clears out");

    expect_status(adif_merge_export("ADIF EXPORT\n<eoh>\nleftover without eor\n",
                                    incoming, out),
                  AdifMergeStatus::PARSE_ARCHIVE, "leftover text");

    expect_status(adif_merge_export("ADIF EXPORT\n<eoh>\n<eor>\n", incoming, out),
                  AdifMergeStatus::PARSE_ARCHIVE, "empty eor missing fields");

    const std::string no_header = k1 + "\n" + k2 + "\n";
    expect_status(adif_merge_export("", no_header, out), AdifMergeStatus::OK,
                  "records without eoh still parse");
    parsed = must_parse(out, "parse no-header");
    expect_true(parsed.size() == 2, "no-header two records");

    if (g_fails != 0) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    printf("PASS: adif merge export\n");
    return 0;
}
