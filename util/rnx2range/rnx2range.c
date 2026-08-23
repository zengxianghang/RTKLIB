#include "rnxrange.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *stream)
{
    fprintf(stream,
        "Usage: rnx2range -i INPUT.obs -o OUTPUT.rangea [options]\n"
        "\n"
        "Convert every RINEX observation supported by the NovAtel OEM7 RANGE\n"
        "RINEX mapping table to RANGEA. Use - for stdin or stdout.\n"
        "\n"
        "Options:\n"
        "  --strict                 fail if a supported value cannot be converted (default)\n"
        "  --no-strict              emit best-effort output and report conversion errors\n"
        "  --fail-on-unsupported    fail if OEM7 has no mapping for any source value\n"
        "  --assume-snr-dbhz        treat S observations as dB-Hz without a DBHZ header\n"
        "  --psr-sigma METERS       synthetic pseudorange sigma (default 0.500)\n"
        "  --adr-sigma CYCLES       synthetic ADR sigma (default 0.050)\n"
        "  --port NAME              synthetic ASCII header port (default COM1)\n"
        "  --time-status NAME       synthetic time status (default FINE)\n"
        "  --idle-time PERCENT      synthetic receiver idle time (default 0.0)\n"
        "  --receiver-status HEX    synthetic receiver status (default 00000000)\n"
        "  --reserved HEX           synthetic reserved header field (default 0)\n"
        "  --software-version N     synthetic receiver build number (default 0)\n"
        "  --bds-d2-max-prn N       use BDS D2 status for B1/B2/B3 PRNs 1..N (default 5)\n"
        "  --quiet                  suppress per-value warnings; summary is still printed\n"
        "  -h, --help               show this help\n");
}

static int need_value(int argc, char **argv, int *index, const char **value)
{
    if (*index+1>=argc) {
        fprintf(stderr,"rnx2range: option %s requires a value\n",argv[*index]);
        return 0;
    }
    *value=argv[++*index];
    return 1;
}

static int parse_double_arg(const char *text, double *value)
{
    char *end;
    errno=0;
    *value=strtod(text,&end);
    return !errno&&end!=text&&!*end;
}

static int parse_unsigned_arg(const char *text, int base, unsigned int *value)
{
    char *end;
    unsigned long parsed;
    errno=0;
    parsed=strtoul(text,&end,base);
    if (errno||end==text||*end||parsed>0xFFFFFFFFUL) return 0;
    *value=(unsigned int)parsed;
    return 1;
}

static int copy_option(char *destination, size_t size, const char *value,
                       const char *name)
{
    if (strlen(value)>=size) {
        fprintf(stderr,"rnx2range: %s is too long\n",name);
        return 0;
    }
    strcpy(destination,value);
    return 1;
}

int main(int argc, char **argv)
{
    rnxrange_options_t options;
    rnxrange_report_t report;
    const char *input_path=NULL,*output_path=NULL,*value;
    char error_message[512];
    unsigned int parsed_unsigned;
    int i,status;
    rnxrange_default_options(&options);
    for (i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-h")||!strcmp(argv[i],"--help")) {
            usage(stdout);
            return 0;
        }
        else if (!strcmp(argv[i],"-i")) {
            if (!need_value(argc,argv,&i,&input_path)) return 2;
        }
        else if (!strcmp(argv[i],"-o")) {
            if (!need_value(argc,argv,&i,&output_path)) return 2;
        }
        else if (!strcmp(argv[i],"--strict")) options.strict=1;
        else if (!strcmp(argv[i],"--no-strict")) options.strict=0;
        else if (!strcmp(argv[i],"--fail-on-unsupported"))
            options.fail_on_unsupported=1;
        else if (!strcmp(argv[i],"--assume-snr-dbhz"))
            options.assume_snr_dbhz=1;
        else if (!strcmp(argv[i],"--quiet")) options.emit_warnings=0;
        else if (!strcmp(argv[i],"--psr-sigma")) {
            if (!need_value(argc,argv,&i,&value)||
                !parse_double_arg(value,&options.psr_sigma)) return 2;
        }
        else if (!strcmp(argv[i],"--adr-sigma")) {
            if (!need_value(argc,argv,&i,&value)||
                !parse_double_arg(value,&options.adr_sigma)) return 2;
        }
        else if (!strcmp(argv[i],"--idle-time")) {
            if (!need_value(argc,argv,&i,&value)||
                !parse_double_arg(value,&options.idle_time)) return 2;
        }
        else if (!strcmp(argv[i],"--port")) {
            if (!need_value(argc,argv,&i,&value)||
                !copy_option(options.port,sizeof(options.port),value,"port")) return 2;
        }
        else if (!strcmp(argv[i],"--time-status")) {
            if (!need_value(argc,argv,&i,&value)||
                !copy_option(options.time_status,sizeof(options.time_status),value,
                             "time status")) return 2;
        }
        else if (!strcmp(argv[i],"--receiver-status")) {
            if (!need_value(argc,argv,&i,&value)||
                !parse_unsigned_arg(value,16,&options.receiver_status)) return 2;
        }
        else if (!strcmp(argv[i],"--reserved")) {
            if (!need_value(argc,argv,&i,&value)||
                !parse_unsigned_arg(value,16,&options.reserved)) return 2;
        }
        else if (!strcmp(argv[i],"--software-version")) {
            if (!need_value(argc,argv,&i,&value)||
                !parse_unsigned_arg(value,10,&options.software_version)) return 2;
        }
        else if (!strcmp(argv[i],"--bds-d2-max-prn")) {
            if (!need_value(argc,argv,&i,&value)||
                !parse_unsigned_arg(value,10,&parsed_unsigned)||parsed_unsigned>63)
                return 2;
            options.bds_d2_max_prn=(int)parsed_unsigned;
        }
        else {
            fprintf(stderr,"rnx2range: unknown option %s\n",argv[i]);
            usage(stderr);
            return 2;
        }
    }
    if (!input_path||!output_path) {
        usage(stderr);
        return 2;
    }
    status=rnxrange_convert_file(input_path,output_path,&options,&report,stderr,
                                 error_message,sizeof(error_message));
    rnxrange_print_report(stderr,&report);
    if (!status) {
        fprintf(stderr,"rnx2range: failed: %s\n",
                error_message[0]?error_message:"unknown conversion error");
        return 1;
    }
    return 0;
}
