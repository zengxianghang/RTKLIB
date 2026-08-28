#include "rnxrange.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGRAM "rnx2range"

static void usage(FILE *stream)
{
    fprintf(stream,
        "Usage: " PROGRAM " -i INPUT -o OUTPUT [options]\n"
        "\n"
        "Convert RINEX observation data to NovAtel OEM7 ASCII RANGEA.\n"
        "Use '-' for stdin/stdout. Only RANGEA is emitted.\n"
        "\n"
        "Options:\n"
        "  -i FILE                    input RINEX observation file\n"
        "  -o FILE                    output RANGEA file\n"
        "  --strict                   fail on supported values that cannot be converted (default)\n"
        "  --no-strict                report conversion errors but keep best-effort output\n"
        "  --fail-on-unsupported      fail if OEM7 has no representation for any value\n"
        "  --assume-snr-dbhz          treat RINEX S values as dB-Hz without DBHZ header metadata\n"
        "  --no-warnings              suppress per-value warning diagnostics\n"
        "  --psr-sigma M              synthetic pseudorange sigma in metres (default 0.500)\n"
        "  --adr-sigma CYC            synthetic ADR sigma in cycles (default 0.050)\n"
        "  --port WORD                synthetic OEM7 header port (default COM1)\n"
        "  --time-status WORD         synthetic OEM7 time status (default FINE)\n"
        "  --idle-time PERCENT        synthetic OEM7 idle-time percentage (default 0.0)\n"
        "  --receiver-status VALUE    synthetic 32-bit receiver status; decimal or 0xHEX\n"
        "  --reserved VALUE           synthetic OEM7 reserved header field; decimal or 0xHEX\n"
        "  --software-version VALUE   synthetic OEM7 software version (0..65535)\n"
        "  --bds-d2-max-prn N         BDS PRNs 1..N use D2 tracking types (default 5)\n"
        "  -h, --help                 show this help\n"
        "\n"
        "The conversion summary is written to stderr. Fields unavailable in RINEX are\n"
        "synthetic and are counted in synthetic_fields_by_name. Unsupported target\n"
        "signals are warned and summarized unless --fail-on-unsupported is used.\n");
}

static int require_value(int argc, char **argv, int *index, const char **value)
{
    if (*index+1>=argc) {
        fprintf(stderr,PROGRAM ": option %s requires a value\n",argv[*index]);
        return 0;
    }
    *value=argv[++*index];
    return 1;
}

static int parse_double_arg(const char *name, const char *text, double *value)
{
    char *end;
    double parsed;
    errno=0;
    parsed=strtod(text,&end);
    if (errno||end==text||*end) {
        fprintf(stderr,PROGRAM ": invalid %s value: %s\n",name,text);
        return 0;
    }
    *value=parsed;
    return 1;
}

static int parse_uint_arg(const char *name, const char *text,
                          unsigned int maximum, unsigned int *value)
{
    char *end;
    unsigned long parsed;
    if (text[0]=='-') {
        fprintf(stderr,PROGRAM ": invalid %s value: %s\n",name,text);
        return 0;
    }
    errno=0;
    parsed=strtoul(text,&end,0);
    if (errno||end==text||*end||parsed>maximum||parsed>UINT_MAX) {
        fprintf(stderr,PROGRAM ": invalid %s value: %s\n",name,text);
        return 0;
    }
    *value=(unsigned int)parsed;
    return 1;
}

static int copy_word_arg(const char *name, const char *text,
                         char *destination, size_t size)
{
    if (!text[0]||strlen(text)>=size) {
        fprintf(stderr,PROGRAM ": invalid %s value: %s\n",name,text);
        return 0;
    }
    strcpy(destination,text);
    return 1;
}

int main(int argc, char **argv)
{
    rnxrange_options_t options;
    rnxrange_report_t report;
    const char *input=NULL,*output=NULL,*value;
    char error[512];
    int i,status;

    rnxrange_default_options(&options);

    for (i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-h")||!strcmp(argv[i],"--help")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(argv[i],"-i")) {
            if (!require_value(argc,argv,&i,&input)) return 2;
        }
        else if (!strcmp(argv[i],"-o")) {
            if (!require_value(argc,argv,&i,&output)) return 2;
        }
        else if (!strcmp(argv[i],"--strict")) options.strict=1;
        else if (!strcmp(argv[i],"--no-strict")) options.strict=0;
        else if (!strcmp(argv[i],"--fail-on-unsupported"))
            options.fail_on_unsupported=1;
        else if (!strcmp(argv[i],"--assume-snr-dbhz"))
            options.assume_snr_dbhz=1;
        else if (!strcmp(argv[i],"--no-warnings")) options.emit_warnings=0;
        else if (!strcmp(argv[i],"--psr-sigma")) {
            if (!require_value(argc,argv,&i,&value)||
                !parse_double_arg("--psr-sigma",value,&options.psr_sigma)) return 2;
        }
        else if (!strcmp(argv[i],"--adr-sigma")) {
            if (!require_value(argc,argv,&i,&value)||
                !parse_double_arg("--adr-sigma",value,&options.adr_sigma)) return 2;
        }
        else if (!strcmp(argv[i],"--idle-time")) {
            if (!require_value(argc,argv,&i,&value)||
                !parse_double_arg("--idle-time",value,&options.idle_time)) return 2;
        }
        else if (!strcmp(argv[i],"--port")) {
            if (!require_value(argc,argv,&i,&value)||
                !copy_word_arg("--port",value,options.port,sizeof(options.port))) return 2;
        }
        else if (!strcmp(argv[i],"--time-status")) {
            if (!require_value(argc,argv,&i,&value)||
                !copy_word_arg("--time-status",value,options.time_status,
                               sizeof(options.time_status))) return 2;
        }
        else if (!strcmp(argv[i],"--receiver-status")) {
            if (!require_value(argc,argv,&i,&value)||
                !parse_uint_arg("--receiver-status",value,UINT_MAX,
                                &options.receiver_status)) return 2;
        }
        else if (!strcmp(argv[i],"--reserved")) {
            if (!require_value(argc,argv,&i,&value)||
                !parse_uint_arg("--reserved",value,UINT_MAX,
                                &options.reserved)) return 2;
        }
        else if (!strcmp(argv[i],"--software-version")) {
            if (!require_value(argc,argv,&i,&value)||
                !parse_uint_arg("--software-version",value,65535U,
                                &options.software_version)) return 2;
        }
        else if (!strcmp(argv[i],"--bds-d2-max-prn")) {
            unsigned int parsed;
            if (!require_value(argc,argv,&i,&value)||
                !parse_uint_arg("--bds-d2-max-prn",value,63U,&parsed)) return 2;
            options.bds_d2_max_prn=(int)parsed;
        }
        else {
            fprintf(stderr,PROGRAM ": unknown option: %s\n",argv[i]);
            usage(stderr);
            return 2;
        }
    }

    if (!input||!output) {
        fprintf(stderr,PROGRAM ": both -i and -o are required\n");
        usage(stderr);
        return 2;
    }

    status=rnxrange_convert_file(input,output,&options,&report,stderr,error,
                                 sizeof(error));
    if (!status) {
        fprintf(stderr,PROGRAM ": conversion failed: %s\n",
                error[0]?error:"unspecified error");
        if (strcmp(output,"-")) remove(output);
        return 1;
    }
    rnxrange_print_report(stderr,&report);
    return 0;
}
