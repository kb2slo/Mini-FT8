/*
 * ADIF copy-to-SD merge: union onto the archive, never drop a unique QSO,
 * never duplicate station+call+date+time_on.
 */
#include <cstdio>
#include <cstring>
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

// --- logger record formatting ------------------------------------------

static void expect_eq_str(const std::string& got, const std::string& want, const char* msg) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s\n  got  [%s]\n  want [%s]\n", msg, got.c_str(), want.c_str());
        ++g_fails;
    }
}

static AdifLogFields sample_fields() {
    AdifLogFields f;
    f.call             = "KE4PLU";
    f.gridsquare       = "EM58";
    f.mode             = "FT8";
    f.qso_date         = "20260908";
    f.time_on          = "142600";
    f.freq             = "14.074";
    f.station_callsign = "KB2SLO";
    f.my_gridsquare    = "EM10";
    f.rst_sent         = 14;
    f.rst_rcvd         = -9;
    f.comment          = "MiniFT8";
    return f;
}

static void test_log_record_byte_compatible() {
    // Byte-for-byte against a real record from a field log (2026-09-08), so a
    // refactor cannot silently change the on-disk layout. Note there is no
    // space between <mode:3>FT8 and <qso_date:8>: that is how this logger has
    // always written it, and merge keys depend on the record text.
    const std::string want =
        "<call:6>KE4PLU <gridsquare:4>EM58 <mode:3>FT8<qso_date:8>20260908 "
        "<time_on:6>142600 <freq:6>14.074 <station_callsign:6>KB2SLO "
        "<my_gridsquare:4>EM10 <rst_sent:2>14 <rst_rcvd:2>-9 <comment:7>MiniFT8 <eor>\n";
    expect_eq_str(adif_format_log_record(sample_fields()), want, "log record byte layout");
}

static void test_log_record_omits_empty_grid() {
    // REGRESSION: a QSO where the DX never sent a grid used to emit
    // "<gridsquare:0> ". Observed twice in the 2026-09-08 field log (N2FSM,
    // W4MAA). The field must be absent, not zero-length.
    AdifLogFields f = sample_fields();
    f.call = "N2FSM";
    f.gridsquare.clear();
    const std::string out = adif_format_log_record(f);
    expect_true(out.find("<gridsquare:") == std::string::npos,
                "REGRESSION: empty grid must omit the field, not write <gridsquare:0>");
    expect_true(out.find("<call:5>N2FSM <mode:3>FT8") != std::string::npos,
                "call runs straight into mode when the grid is absent");
}

static void test_log_record_omits_unset_fields() {
    AdifLogFields f = sample_fields();
    f.rst_sent = kAdifNoReport;
    f.rst_rcvd = kAdifNoReport;
    f.comment.clear();
    f.my_gridsquare.clear();
    const std::string out = adif_format_log_record(f);
    expect_true(out.find("<rst_sent:") == std::string::npos, "unset rst_sent omitted");
    expect_true(out.find("<rst_rcvd:") == std::string::npos, "unset rst_rcvd omitted");
    expect_true(out.find("<comment:") == std::string::npos, "empty comment omitted");
    expect_true(out.find("<my_gridsquare:") == std::string::npos, "empty my_gridsquare omitted");
    // A report of 0 is a real value and must survive; only -99 means "none".
    f.rst_sent = 0;
    expect_true(adif_format_log_record(f).find("<rst_sent:1>0 ") != std::string::npos,
                "rst_sent 0 is a real report, not 'unset'");
}

static void test_log_record_lengths_match_values() {
    // The length prefix is where a hand-built record goes wrong silently.
    // Check every field of a record with awkward values.
    AdifLogFields f = sample_fields();
    f.call        = "VU2OY";
    f.gridsquare  = "MK68";
    f.rst_sent    = -100;      // 4 chars including the sign
    f.rst_rcvd    = 5;         // 1 char
    f.comment     = "MiniFT8 QMX /P";
    const std::string out = adif_format_log_record(f);
    size_t pos = 0;
    while ((pos = out.find('<', pos)) != std::string::npos) {
        size_t colon = out.find(':', pos);
        size_t close = out.find('>', pos);
        if (colon == std::string::npos || close == std::string::npos || colon > close) break;
        const std::string tag = out.substr(pos + 1, colon - pos - 1);
        if (tag == "eor") break;
        const int declared = atoi(out.substr(colon + 1, close - colon - 1).c_str());
        // The value runs to the next '<'.
        size_t next = out.find('<', close);
        std::string value = out.substr(close + 1, next - close - 1);
        while (!value.empty() && value.back() == ' ') value.pop_back();
        if ((int)value.size() != declared) {
            fprintf(stderr, "FAIL: <%s:%d> but value \"%s\" is %d bytes\n",
                    tag.c_str(), declared, value.c_str(), (int)value.size());
            ++g_fails;
        }
        pos = close;
    }
}

static void test_log_record_roundtrips_through_parser() {
    // The formatter and the parser must agree: a freshly written record has to
    // survive adif_parse(), which is what copy-to-SD merge runs on.
    const std::string doc = "ADIF EXPORT\n<eoh>\n" + adif_format_log_record(sample_fields());
    std::vector<AdifRecord> recs;
    expect_true(adif_parse(doc, recs), "formatted record parses");
    expect_true(recs.size() == 1, "one record parsed");
    if (recs.size() == 1) {
        expect_true(!recs[0].key.empty(), "parsed record has a merge key");
    }
    // And the same for a record with the grid omitted.
    AdifLogFields f = sample_fields();
    f.gridsquare.clear();
    const std::string doc2 = "ADIF EXPORT\n<eoh>\n" + adif_format_log_record(f);
    std::vector<AdifRecord> recs2;
    expect_true(adif_parse(doc2, recs2), "grid-less record parses");
    expect_true(recs2.size() == 1, "one grid-less record parsed");
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

    {
        AdifStream st;
        std::vector<AdifRecord> chunked;
        bool ok = true;
        for (char ch : incoming) {
            if (!adif_stream_feed(&st, &ch, 1, false, [](const AdifRecord& rec, void* ctx) {
                    static_cast<std::vector<AdifRecord>*>(ctx)->push_back(rec);
                    return true;
                }, &chunked)) {
                ok = false;
                break;
            }
        }
        ok = ok && adif_stream_feed(&st, nullptr, 0, true, [](const AdifRecord& rec, void* ctx) {
            static_cast<std::vector<AdifRecord>*>(ctx)->push_back(rec);
            return true;
        }, &chunked);
        expect_true(ok && chunked.size() == 2, "1-byte stream feed");
    }

    {
        std::FILE* arch = std::tmpfile();
        std::FILE* inc = std::tmpfile();
        std::FILE* merged = std::tmpfile();
        expect_true(arch && inc && merged, "tmpfile");
        if (arch && inc && merged) {
            std::fwrite(archive.data(), 1, archive.size(), arch);
            std::fwrite(grown.data(), 1, grown.size(), inc);
            std::rewind(arch);
            std::rewind(inc);
            expect_status(adif_merge_stdio(arch, inc, merged), AdifMergeStatus::OK,
                          "stdio merge");
            std::rewind(merged);
            std::string streamed;
            char buf[256];
            size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof(buf), merged)) > 0) {
                streamed.append(buf, n);
            }
            std::string in_memory;
            expect_status(adif_merge_export(archive, grown, in_memory), AdifMergeStatus::OK,
                          "memory merge for stdio compare");
            expect_true(streamed == in_memory, "stdio merge matches memory merge");
            parsed = must_parse(streamed, "parse stdio merge");
            expect_true(parsed.size() == 3, "stdio union has three");
        }
        if (arch) {
            std::fclose(arch);
        }
        if (inc) {
            std::fclose(inc);
        }
        if (merged) {
            std::fclose(merged);
        }
    }

    {
        AdifLoggerDedupe d;
        expect_true(!adif_logger_dedupe_is_duplicate(&d, "", 0), "empty never dup");
        adif_logger_dedupe_remember(&d, "", 0);
        expect_true(d.recent.empty(), "empty remember is no-op");

        adif_logger_dedupe_remember(&d, "K1ABC", 1000);
        expect_true(adif_logger_dedupe_is_duplicate(&d, "K1ABC", 1000), "same instant");
        expect_true(adif_logger_dedupe_is_duplicate(&d, "K1ABC", 1000 + kAdifLoggerDedupeWindowMs),
                    "still dup at window edge");
        expect_true(!adif_logger_dedupe_is_duplicate(&d, "K1ABC",
                                                    1000 + kAdifLoggerDedupeWindowMs + 1),
                    "expired after window");
        expect_true(!adif_logger_dedupe_is_duplicate(&d, "W1AW", 1000), "other call");

        adif_logger_dedupe_remember(&d, "N0CALL", 2000);
        adif_logger_dedupe_remember(&d, "N0CALL", 2000 + kAdifLoggerDedupeWindowMs);
        expect_true(adif_logger_dedupe_is_duplicate(&d, "N0CALL",
                                                    2000 + kAdifLoggerDedupeWindowMs + 1),
                    "remember refreshes window");

        AdifLoggerDedupe cap;
        for (std::size_t i = 0; i < kAdifLoggerDedupeMaxEntries + 1; ++i) {
            adif_logger_dedupe_remember(&cap, "C" + std::to_string(i), 0);
        }
        expect_true(!adif_logger_dedupe_is_duplicate(&cap, "C0", 0), "oldest dropped at cap");
        expect_true(adif_logger_dedupe_is_duplicate(
                        &cap, "C" + std::to_string(kAdifLoggerDedupeMaxEntries), 0),
                    "newest kept");
    }

    if (g_fails != 0) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    test_log_record_byte_compatible();
    test_log_record_omits_empty_grid();
    test_log_record_omits_unset_fields();
    test_log_record_lengths_match_values();
    test_log_record_roundtrips_through_parser();

    if (g_fails) {
        fprintf(stderr, "FAILED: %d check(s)\n", g_fails);
        return 1;
    }
    printf("PASS: adif merge export, logger dedupe, and record formatting\n");
    return 0;
}
