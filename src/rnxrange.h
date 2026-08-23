#ifndef RNXRANGE_H
#define RNXRANGE_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RNXRANGE_VALUE_TYPES 5 /* C, L, D, S, X */

typedef struct {
    char port[32];
    char time_status[32];
    double idle_time;
    unsigned int receiver_status;
    unsigned int reserved;
    unsigned int software_version;
    double psr_sigma;
    double adr_sigma;
    int strict;
    int fail_on_unsupported;
    int assume_snr_dbhz;
    int emit_warnings;
    int bds_d2_max_prn;
} rnxrange_options_t;

typedef struct {
    unsigned long input_epochs;
    unsigned long output_messages;
    unsigned long source_satellites;
    unsigned long source_signal_records;
    unsigned long source_nonempty_values;
    unsigned long source_nonempty_by_type[RNXRANGE_VALUE_TYPES];
    unsigned long emitted_range_records;
    unsigned long converted_values;
    unsigned long converted_by_type[RNXRANGE_VALUE_TYPES];
    unsigned long skipped_unsupported_values;
    unsigned long skipped_unsupported_by_type[RNXRANGE_VALUE_TYPES];
    unsigned long conversion_errors;
    unsigned long synthetic_adr;
    unsigned long synthetic_doppler;
    unsigned long synthetic_cno;
    unsigned long synthetic_psr_sigma;
    unsigned long synthetic_adr_sigma;
    unsigned long synthetic_channel;
    unsigned long synthetic_locktime;
    unsigned long synthetic_half_cycle;
    unsigned long synthetic_tracking_state;
    unsigned long synthetic_bds_data_type;
    unsigned long synthetic_header_port;
    unsigned long synthetic_header_idle_time;
    unsigned long synthetic_header_time_status;
    unsigned long synthetic_header_receiver_status;
    unsigned long synthetic_header_reserved;
    unsigned long synthetic_header_software_version;
    int first_week;
    int last_week;
    double first_tow;
    double last_tow;
    int have_time_span;
} rnxrange_report_t;

void rnxrange_default_options(rnxrange_options_t *options);

int rnxrange_convert(FILE *input, FILE *output,
                     const rnxrange_options_t *options,
                     rnxrange_report_t *report, FILE *diagnostic,
                     char *error_message, size_t error_message_size);

int rnxrange_convert_file(const char *input_path, const char *output_path,
                          const rnxrange_options_t *options,
                          rnxrange_report_t *report, FILE *diagnostic,
                          char *error_message, size_t error_message_size);

void rnxrange_print_report(FILE *stream, const rnxrange_report_t *report);

#ifdef __cplusplus
}
#endif

#endif
