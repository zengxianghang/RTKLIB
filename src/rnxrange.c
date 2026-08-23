/*
 * Capacity-preserving RINEX observation to NovAtel OEM7 RANGEA converter.
 *
 * Format references:
 *   RINEX 2.11: https://files.igs.org/pub/data/format/rinex211.txt
 *   RINEX 3.05: https://files.igs.org/pub/data/format/rinex305.pdf
 *   RINEX 4.02: https://files.igs.org/pub/data/format/rinex_4.02.pdf
 *   OEM7 RANGE: https://docs.novatel.com/OEM7/Content/Logs/RANGE.htm
 *   OEM7 ASCII: https://docs.novatel.com/OEM7/Content/Messages/ASCII.htm
 *   OEM7 PRNs:  https://docs.novatel.com/OEM7/Content/Messages/PRN_Numbers.htm
 *   OEM7 manual: https://docs.novatel.com/oem7/Content/PDFs/OEM7_Commands_Logs_Manual.pdf
 */
#include "rtklib.h"
#include "rnxrange.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define RNXRANGE_MAX_TYPES 4096
#define RNXRANGE_REASON_SIZE 96

typedef struct {
    char sys;
    char signal[3];
    int system_bits;
    int signal_type;
    double phase_shift;
} signal_map_t;

typedef struct {
    char sys;
    int rinex_min;
    int rinex_max;
    int range_offset;
} system_prn_map_t;

/* OEM7 RANGE RINEX Mappings table, including firmware 7.10.02 entries. */
static const signal_map_t signal_maps[]={
    {'G',"1C",0, 0, 0.00}, {'G',"1L",0,16, 0.25},
    {'G',"2S",0,17,-0.25}, {'G',"2P",0, 5, 0.00},
    {'G',"2W",0, 9, 0.00}, {'G',"5Q",0,14,-0.25},

    {'R',"1C",1, 0, 0.00}, {'R',"2C",1, 1, 0.00},
    {'R',"2P",1, 5, 0.25}, {'R',"3Q",1, 6, 0.25},

    {'S',"1C",2, 0, 0.00}, {'S',"5I",2, 6, 0.00},

    {'E',"1C",3, 2, 0.50}, {'E',"5Q",3,12,-0.25},
    {'E',"7Q",3,17,-0.25}, {'E',"8Q",3,20,-0.25},
    {'E',"6B",3, 6, 0.00}, {'E',"6C",3, 7,-0.50},

    {'C',"2I",4, 0, 0.00}, {'C',"1P",4, 7, 0.25},
    {'C',"7I",4, 1, 0.00}, {'C',"5P",4, 9, 0.25},
    {'C',"7D",4,11, 0.00}, {'C',"6I",4, 2, 0.00},

    {'J',"1C",5, 0, 0.00}, {'J',"1L",5,16, 0.25},
    {'J',"1E",5,24, 0.00}, {'J',"2S",5,17, 0.00},
    {'J',"5Q",5,14,-0.25}, {'J',"6L",5,27, 0.00},
    {'J',"6S",5,28, 0.00},

    {'I',"5A",6, 0, 0.00}
};

/* RINEX identifiers to OEM7 RANGE PRN/slot numbers. */
static const system_prn_map_t system_prn_maps[]={
    {'G', 1,32,  0}, {'R', 1,24, 37}, {'E', 1,36,  0},
    {'C', 1,63,  0}, {'I', 1,14,  0}, {'J', 1,10,192},
    {'S',20,58,100}
};

typedef struct {
    FILE *fp;
    unsigned long line_number;
} line_reader_t;

typedef struct {
    char sys;
    size_t n;
    char (*types)[4];
    double *scale;
} system_types_t;

enum {
    RNX_TIME_GPS=0,
    RNX_TIME_UTC,
    RNX_TIME_BDT,
    RNX_TIME_GAL,
    RNX_TIME_QZS,
    RNX_TIME_IRN
};

typedef struct {
    double version;
    char file_type;
    char file_system;
    int time_system;
    int clock_offsets_applied;
    int snr_dbhz;
    char snr_unit[21];
    system_types_t systems[7];
    system_types_t v2_types;
    int glo_fcn[100];
} rinex_header_t;

typedef struct {
    char code[4];
    double value;
    unsigned char lli;
    unsigned char ssi;
    int present;
} rinex_value_t;

typedef struct {
    char sys;
    int rinex_prn;
    char id[4];
    size_t n;
    rinex_value_t *values;
} satellite_obs_t;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} string_buffer_t;

typedef struct {
    char sys;
    int prn;
    char signal[3];
    gtime_t last_time;
    double locktime;
    unsigned long last_epoch;
    int active;
} lock_state_t;

typedef struct {
    char sys;
    char code[4];
    unsigned long count;
} code_count_t;

typedef struct {
    char category[20];
    char reason[RNXRANGE_REASON_SIZE];
    unsigned long count;
} reason_count_t;

typedef struct {
    line_reader_t reader;
    rinex_header_t header;
    const rnxrange_options_t *options;
    rnxrange_report_t *report;
    FILE *diagnostic;
    char *error_message;
    size_t error_message_size;
    lock_state_t *locks;
    size_t lock_count;
    size_t lock_capacity;
    code_count_t *code_counts;
    size_t code_count;
    size_t code_capacity;
    reason_count_t *reason_counts;
    size_t reason_count;
    size_t reason_capacity;
    int had_conversion_error;
    int had_unsupported;
} converter_t;

static const char value_type_names[RNXRANGE_VALUE_TYPES]={'C','L','D','S','X'};

static void set_error(converter_t *converter, const char *format, ...)
{
    va_list ap;
    if (!converter->error_message||converter->error_message_size==0) return;
    va_start(ap,format);
    vsnprintf(converter->error_message,converter->error_message_size,format,ap);
    va_end(ap);
}

static int read_line(line_reader_t *reader, char **line)
{
    size_t len=0,cap=256;
    int c;
    char *buffer=(char *)malloc(cap);
    if (!buffer) return -1;
    while ((c=fgetc(reader->fp))!=EOF) {
        if (len+1>=cap) {
            char *expanded;
            if (cap>((size_t)-1)/2) {
                free(buffer);
                return -1;
            }
            cap*=2;
            if (!(expanded=(char *)realloc(buffer,cap))) {
                free(buffer);
                return -1;
            }
            buffer=expanded;
        }
        if (c=='\n') break;
        if (c!='\r') buffer[len++]=(char)c;
    }
    if (c==EOF&&len==0) {
        free(buffer);
        return 0;
    }
    buffer[len]='\0';
    reader->line_number++;
    *line=buffer;
    return 1;
}

static void copy_columns(char *destination, size_t destination_size,
                         const char *line, size_t start, size_t width)
{
    size_t line_length=strlen(line),n=0;
    if (destination_size==0) return;
    if (start<line_length) {
        n=line_length-start;
        if (n>width) n=width;
        if (n>=destination_size) n=destination_size-1;
        memcpy(destination,line+start,n);
    }
    destination[n]='\0';
}

static void trim(char *text)
{
    char *first=text;
    size_t length;
    while (*first&&isspace((unsigned char)*first)) first++;
    if (first!=text) memmove(text,first,strlen(first)+1);
    length=strlen(text);
    while (length>0&&isspace((unsigned char)text[length-1])) text[--length]='\0';
}

static int line_has_label(const char *line, const char *label)
{
    return strlen(line)>=60&&strstr(line+60,label)!=NULL;
}

static int parse_int_columns(const char *line, size_t start, size_t width,
                             int *value)
{
    char field[64],*end;
    long parsed;
    if (width>=sizeof(field)) return 0;
    copy_columns(field,sizeof(field),line,start,width);
    trim(field);
    if (!field[0]) return 0;
    errno=0;
    parsed=strtol(field,&end,10);
    if (errno||*end||parsed<INT_MIN||parsed>INT_MAX) return 0;
    *value=(int)parsed;
    return 1;
}

static int parse_double_columns(const char *line, size_t start, size_t width,
                                double *value)
{
    char field[128],*end,*p;
    double parsed;
    if (width>=sizeof(field)) return 0;
    copy_columns(field,sizeof(field),line,start,width);
    trim(field);
    if (!field[0]) return 0;
    for (p=field;*p;p++) if (*p=='D'||*p=='d') *p='E';
    errno=0;
    parsed=strtod(field,&end);
    if (errno||*end||!isfinite(parsed)) return 0;
    *value=parsed;
    return 1;
}

static int system_index(char sys)
{
    const char *systems="GREJSCI";
    const char *position=strchr(systems,sys);
    return position?(int)(position-systems):-1;
}

static system_types_t *get_system_types(rinex_header_t *header, char sys)
{
    int index=system_index(sys);
    return index>=0?header->systems+index:NULL;
}

static void free_system_types(system_types_t *types)
{
    free(types->types);
    free(types->scale);
    memset(types,0,sizeof(*types));
}

static void free_header(rinex_header_t *header)
{
    int i;
    for (i=0;i<7;i++) free_system_types(header->systems+i);
    free_system_types(&header->v2_types);
}

static int allocate_types(converter_t *converter, system_types_t *types,
                          char sys, size_t count)
{
    size_t i;
    if (count==0||count>RNXRANGE_MAX_TYPES) {
        set_error(converter,"line %lu: invalid observation type count %lu",
                  converter->reader.line_number,(unsigned long)count);
        return 0;
    }
    free_system_types(types);
    types->types=(char (*)[4])calloc(count,sizeof(*types->types));
    types->scale=(double *)malloc(count*sizeof(*types->scale));
    if (!types->types||!types->scale) {
        set_error(converter,"out of memory allocating %lu observation types",
                  (unsigned long)count);
        free_system_types(types);
        return 0;
    }
    types->sys=sys;
    types->n=count;
    for (i=0;i<count;i++) types->scale[i]=1.0;
    return 1;
}

static int parse_v3_types(converter_t *converter, char *first_line)
{
    int count_value,index;
    size_t count,parsed=0,slot;
    char sys=first_line[0],*line=first_line;
    system_types_t *types=get_system_types(&converter->header,sys);
    if (!types||!parse_int_columns(first_line,3,3,&count_value)||count_value<=0) {
        set_error(converter,"line %lu: invalid SYS / # / OBS TYPES record",
                  converter->reader.line_number);
        return 0;
    }
    count=(size_t)count_value;
    if (!allocate_types(converter,types,sys,count)) return 0;
    while (parsed<count) {
        for (slot=0;slot<13&&parsed<count;slot++) {
            char code[4];
            copy_columns(code,sizeof(code),line,7+slot*4,3);
            trim(code);
            if (strlen(code)!=3&&!(strlen(code)==2&&code[0]=='X')) {
                if (line!=first_line) free(line);
                set_error(converter,"line %lu: missing observation code %lu of %lu",
                          converter->reader.line_number,(unsigned long)(parsed+1),
                          (unsigned long)count);
                return 0;
            }
            memcpy(types->types[parsed],code,4);
            parsed++;
        }
        if (parsed<count) {
            if (line!=first_line) free(line);
            index=read_line(&converter->reader,&line);
            if (index<=0) {
                set_error(converter,"unexpected end of SYS / # / OBS TYPES record");
                return 0;
            }
            if (!line_has_label(line,"SYS / # / OBS TYPES")) {
                free(line);
                set_error(converter,"line %lu: invalid observation type continuation",
                          converter->reader.line_number);
                return 0;
            }
        }
    }
    if (line!=first_line) free(line);
    return 1;
}

static int parse_v2_types(converter_t *converter, char *first_line)
{
    int count_value,status;
    size_t count,parsed=0,slot;
    char *line=first_line;
    system_types_t *types=&converter->header.v2_types;
    if (!parse_int_columns(first_line,0,6,&count_value)||count_value<=0) {
        set_error(converter,"line %lu: invalid # / TYPES OF OBSERV record",
                  converter->reader.line_number);
        return 0;
    }
    count=(size_t)count_value;
    if (!allocate_types(converter,types,'M',count)) return 0;
    while (parsed<count) {
        for (slot=0;slot<9&&parsed<count;slot++) {
            char code[4];
            copy_columns(code,sizeof(code),line,10+slot*6,2);
            trim(code);
            if (strlen(code)!=2) {
                if (line!=first_line) free(line);
                set_error(converter,"line %lu: missing RINEX 2 observation code",
                          converter->reader.line_number);
                return 0;
            }
            memcpy(types->types[parsed],code,3);
            parsed++;
        }
        if (parsed<count) {
            if (line!=first_line) free(line);
            status=read_line(&converter->reader,&line);
            if (status<=0||!line_has_label(line,"# / TYPES OF OBSERV")) {
                free(line);
                set_error(converter,"line %lu: invalid RINEX 2 observation type continuation",
                          converter->reader.line_number);
                return 0;
            }
        }
    }
    if (line!=first_line) free(line);
    return 1;
}

static int type_position(const system_types_t *types, const char *code)
{
    size_t i;
    for (i=0;i<types->n;i++) if (!strcmp(types->types[i],code)) return (int)i;
    return -1;
}

static int parse_scale_factor(converter_t *converter, char *first_line)
{
    char sys=first_line[0],*line=first_line;
    system_types_t *types=get_system_types(&converter->header,sys);
    int factor,count,status,position;
    size_t parsed=0,slot,i;
    if (!types||types->n==0||!parse_int_columns(first_line,2,4,&factor)||
        factor<=0) {
        set_error(converter,"line %lu: invalid SYS / SCALE FACTOR record",
                  converter->reader.line_number);
        return 0;
    }
    if (!parse_int_columns(first_line,8,2,&count)) count=0;
    if (count==0) {
        for (i=0;i<types->n;i++) types->scale[i]=(double)factor;
        return 1;
    }
    while (parsed<(size_t)count) {
        for (slot=0;slot<12&&parsed<(size_t)count;slot++) {
            char code[4];
            copy_columns(code,sizeof(code),line,11+slot*4,3);
            trim(code);
            if ((strlen(code)!=3&&!(strlen(code)==2&&code[0]=='X'))||
                (position=type_position(types,code))<0) {
                if (line!=first_line) free(line);
                set_error(converter,"line %lu: invalid scale-factor observation code %s",
                          converter->reader.line_number,code);
                return 0;
            }
            types->scale[position]=(double)factor;
            parsed++;
        }
        if (parsed<(size_t)count) {
            if (line!=first_line) free(line);
            status=read_line(&converter->reader,&line);
            if (status<=0||!line_has_label(line,"SYS / SCALE FACTOR")) {
                free(line);
                set_error(converter,"line %lu: invalid scale-factor continuation",
                          converter->reader.line_number);
                return 0;
            }
        }
    }
    if (line!=first_line) free(line);
    return 1;
}

static void parse_glonass_fcn(rinex_header_t *header, const char *line)
{
    size_t i,length=strlen(line);
    for (i=0;i+3<length&&i<60;i++) {
        int slot,fcn,consumed=0;
        if (line[i]!='R'||!isdigit((unsigned char)line[i+1])||
            !isdigit((unsigned char)line[i+2])) continue;
        slot=(line[i+1]-'0')*10+(line[i+2]-'0');
        if (sscanf(line+i+3," %d%n",&fcn,&consumed)==1&&consumed>0&&
            1<=slot&&slot<=99&&-7<=fcn&&fcn<=6) {
            header->glo_fcn[slot]=fcn;
            i+=3+(size_t)consumed-1;
        }
    }
}

static int time_system_from_text(const char *text, char file_system)
{
    if (!strncmp(text,"GLO",3)||!strncmp(text,"UTC",3)) return RNX_TIME_UTC;
    if (!strncmp(text,"BDT",3)) return RNX_TIME_BDT;
    if (!strncmp(text,"GAL",3)) return RNX_TIME_GAL;
    if (!strncmp(text,"QZS",3)) return RNX_TIME_QZS;
    if (!strncmp(text,"IRN",3)) return RNX_TIME_IRN;
    if (!strncmp(text,"GPS",3)) return RNX_TIME_GPS;
    if (text[0]) return -1;
    if (file_system=='R') return RNX_TIME_UTC;
    if (file_system=='C') return RNX_TIME_BDT;
    if (file_system=='E') return RNX_TIME_GAL;
    if (file_system=='J') return RNX_TIME_QZS;
    if (file_system=='I') return RNX_TIME_IRN;
    return RNX_TIME_GPS;
}

static int parse_header(converter_t *converter)
{
    rinex_header_t *header=&converter->header;
    char *line=NULL,time_code[4]="";
    int status,i,have_first=0,have_end=0;
    memset(header,0,sizeof(*header));
    header->clock_offsets_applied=0;
    for (i=0;i<100;i++) header->glo_fcn[i]=999;
    while ((status=read_line(&converter->reader,&line))>0) {
        if (!have_first) {
            if (!line_has_label(line,"RINEX VERSION / TYPE")||
                !parse_double_columns(line,0,9,&header->version)) {
                set_error(converter,"line 1: not a RINEX file");
                free(line);
                return 0;
            }
            header->file_type=strlen(line)>20?line[20]:' ';
            header->file_system=strlen(line)>40?line[40]:'G';
            if (header->file_system==' '||header->file_system=='\0')
                header->file_system='G';
            if (header->file_type!='O') {
                set_error(converter,"RINEX file type %c is not observation data",
                          header->file_type);
                free(line);
                return 0;
            }
            if (header->version<2.0||header->version>=5.0) {
                set_error(converter,"unsupported RINEX version %.2f",header->version);
                free(line);
                return 0;
            }
            have_first=1;
        }
        else if (line_has_label(line,"SYS / # / OBS TYPES")) {
            if (!parse_v3_types(converter,line)) { free(line); return 0; }
        }
        else if (line_has_label(line,"# / TYPES OF OBSERV")) {
            if (!parse_v2_types(converter,line)) { free(line); return 0; }
        }
        else if (line_has_label(line,"SYS / SCALE FACTOR")) {
            if (!parse_scale_factor(converter,line)) { free(line); return 0; }
        }
        else if (line_has_label(line,"SIGNAL STRENGTH UNIT")) {
            copy_columns(header->snr_unit,sizeof(header->snr_unit),line,0,20);
            trim(header->snr_unit);
            header->snr_dbhz=!strcmp(header->snr_unit,"DBHZ");
        }
        else if (line_has_label(line,"TIME OF FIRST OBS")) {
            copy_columns(time_code,sizeof(time_code),line,48,3);
            trim(time_code);
        }
        else if (line_has_label(line,"RCV CLOCK OFFS APPL")) {
            if (!parse_int_columns(line,0,6,&header->clock_offsets_applied)||
                (header->clock_offsets_applied!=0&&
                 header->clock_offsets_applied!=1)) {
                set_error(converter,"line %lu: invalid RCV CLOCK OFFS APPL record",
                          converter->reader.line_number);
                free(line);
                return 0;
            }
        }
        else if (line_has_label(line,"GLONASS SLOT / FRQ #")) {
            parse_glonass_fcn(header,line);
        }
        else if (line_has_label(line,"END OF HEADER")) {
            have_end=1;
            free(line);
            break;
        }
        free(line);
        line=NULL;
    }
    if (status<0) {
        set_error(converter,"out of memory while reading RINEX header");
        return 0;
    }
    if (!have_end) {
        set_error(converter,"RINEX header has no END OF HEADER record");
        return 0;
    }
    if (header->file_system=='M'&&!time_code[0]) {
        set_error(converter,
                  "mixed RINEX observation file has no TIME OF FIRST OBS time system");
        return 0;
    }
    header->time_system=time_system_from_text(time_code,header->file_system);
    if (header->time_system<0) {
        set_error(converter,"unsupported RINEX observation time system %s",time_code);
        return 0;
    }
    if (header->version<3.0&&header->v2_types.n==0) {
        set_error(converter,"RINEX 2 header has no observation type list");
        return 0;
    }
    if (header->version>=3.0) {
        int any=0;
        for (i=0;i<7;i++) if (header->systems[i].n) any=1;
        if (!any) {
            set_error(converter,"RINEX header has no SYS / # / OBS TYPES records");
            return 0;
        }
    }
    return 1;
}

static gtime_t to_gpst(gtime_t time, int time_system)
{
    if (time_system==RNX_TIME_UTC) return utc2gpst(time);
    if (time_system==RNX_TIME_BDT) return bdt2gpst(time);
    /* GST, QZS time and IRNSST are aligned to GPST at integer-second level. */
    return time;
}

static const signal_map_t *find_signal_map(char sys, const char *signal)
{
    size_t i;
    for (i=0;i<sizeof(signal_maps)/sizeof(signal_maps[0]);i++) {
        if (signal_maps[i].sys==sys&&!strcmp(signal_maps[i].signal,signal))
            return signal_maps+i;
    }
    return NULL;
}

static int has_v2_code(const system_types_t *types, const char *code)
{
    return type_position(types,code)>=0;
}

static int normalize_v2_code(const rinex_header_t *header, char sys,
                             const char *source, char destination[4])
{
    const system_types_t *types=&header->v2_types;
    char type=source[0],band=source[1],attribute='\0';
    destination[0]='\0';
    if (type=='P') {
        type='C';
        if (sys=='G') attribute='W';
        else if (sys=='R') attribute='P';
        else return 0;
    }
    else if (type=='C') {
        if (band=='1') attribute=(sys=='E'||sys=='C')?'X':'C';
        else if (band=='2') {
            if (sys=='G'||sys=='J') attribute=header->version>=2.12?'W':'X';
            else if (sys=='R') attribute='C';
            else if (sys=='C') { band='1'; attribute='X'; }
        }
    }
    else if (type=='L'||type=='D'||type=='S') {
        if (band=='1') {
            if (sys=='G') attribute=has_v2_code(types,"C1")?'C':'W';
            else if (sys=='R') attribute=has_v2_code(types,"C1")?'C':'P';
            else if (sys=='E'||sys=='C') attribute='X';
            else attribute='C';
        }
        else if (band=='2') {
            if (sys=='G') attribute=has_v2_code(types,"P2")?'W':'X';
            else if (sys=='R') attribute=has_v2_code(types,"P2")?'P':'C';
            else if (sys=='J') attribute='X';
            else if (sys=='C') { band='1'; attribute='X'; }
        }
        else if (band=='5') attribute='X';
        else if (band=='6'||band=='7'||band=='8') attribute='X';
    }
    if (!attribute) return 0;
    destination[0]=type;
    destination[1]=band;
    destination[2]=attribute;
    destination[3]='\0';
    return 1;
}

static int parse_satellite_id(const rinex_header_t *header, const char *field,
                              char *sys, int *prn, char id[4])
{
    char text[8];
    size_t n=strlen(field);
    if (n>=sizeof(text)) n=sizeof(text)-1;
    memcpy(text,field,n);
    text[n]='\0';
    trim(text);
    if (!text[0]) return 0;
    if (isalpha((unsigned char)text[0])) {
        *sys=(char)toupper((unsigned char)text[0]);
        *prn=atoi(text+1);
    }
    else {
        *sys=header->file_system=='M'?'G':header->file_system;
        *prn=atoi(text);
    }
    if (system_index(*sys)<0||*prn<=0) return 0;
    snprintf(id,4,"%c%02d",*sys,*prn%100);
    return 1;
}

static int value_type_index(char type)
{
    int i;
    for (i=0;i<RNXRANGE_VALUE_TYPES;i++) if (value_type_names[i]==type) return i;
    return -1;
}

static void free_satellite(satellite_obs_t *satellite)
{
    free(satellite->values);
    memset(satellite,0,sizeof(*satellite));
}

static int parse_value_field(const char *line, size_t start, double scale,
                             rinex_value_t *value)
{
    char field[32];
    size_t length=strlen(line),i;
    int nonblank=0;
    memset(value,0,sizeof(*value));
    for (i=0;i<14;i++) {
        char c=start+i<length?line[start+i]:' ';
        if (!isspace((unsigned char)c)) nonblank=1;
    }
    if (!nonblank) return 1;
    if (!parse_double_columns(line,start,14,&value->value)) return 0;
    if (scale<=0.0) return 0;
    value->value/=scale;
    value->present=1;
    copy_columns(field,sizeof(field),line,start+14,1);
    if (field[0]>='0'&&field[0]<='9') value->lli=(unsigned char)(field[0]-'0');
    copy_columns(field,sizeof(field),line,start+15,1);
    if (field[0]>='0'&&field[0]<='9') value->ssi=(unsigned char)(field[0]-'0');
    return 1;
}

static int parse_v3_satellite(converter_t *converter, const char *line,
                              satellite_obs_t *satellite)
{
    system_types_t *types;
    char id_field[4];
    size_t i;
    memset(satellite,0,sizeof(*satellite));
    copy_columns(id_field,sizeof(id_field),line,0,3);
    if (!parse_satellite_id(&converter->header,id_field,&satellite->sys,
                            &satellite->rinex_prn,satellite->id)) {
        set_error(converter,"line %lu: invalid satellite identifier",
                  converter->reader.line_number);
        return 0;
    }
    types=get_system_types(&converter->header,satellite->sys);
    if (!types||types->n==0) {
        set_error(converter,"line %lu: no observation types for satellite %s",
                  converter->reader.line_number,satellite->id);
        return 0;
    }
    satellite->n=types->n;
    satellite->values=(rinex_value_t *)calloc(types->n,sizeof(*satellite->values));
    if (!satellite->values) {
        set_error(converter,"out of memory reading satellite %s",satellite->id);
        return 0;
    }
    for (i=0;i<types->n;i++) {
        strcpy(satellite->values[i].code,types->types[i]);
        if (!parse_value_field(line,3+i*16,types->scale[i],satellite->values+i)) {
            set_error(converter,"line %lu: invalid value for %s %s",
                      converter->reader.line_number,satellite->id,types->types[i]);
            free_satellite(satellite);
            return 0;
        }
        strcpy(satellite->values[i].code,types->types[i]);
    }
    return 1;
}

static int read_v2_satellite(converter_t *converter, const char *id,
                             satellite_obs_t *satellite)
{
    const system_types_t *types=&converter->header.v2_types;
    size_t i,line_index,field_index;
    char *line=NULL;
    int status;
    memset(satellite,0,sizeof(*satellite));
    if (!parse_satellite_id(&converter->header,id,&satellite->sys,
                            &satellite->rinex_prn,satellite->id)) {
        set_error(converter,"invalid RINEX 2 satellite identifier %s",id);
        return 0;
    }
    satellite->n=types->n;
    satellite->values=(rinex_value_t *)calloc(types->n,sizeof(*satellite->values));
    if (!satellite->values) {
        set_error(converter,"out of memory reading satellite %s",satellite->id);
        return 0;
    }
    for (line_index=0;line_index<(types->n+4)/5;line_index++) {
        status=read_line(&converter->reader,&line);
        if (status<=0) {
            set_error(converter,"unexpected end of RINEX 2 observations for %s",
                      satellite->id);
            free_satellite(satellite);
            return 0;
        }
        for (field_index=0;field_index<5;field_index++) {
            char normalized[4];
            i=line_index*5+field_index;
            if (i>=types->n) break;
            if (!normalize_v2_code(&converter->header,satellite->sys,
                                   types->types[i],normalized)) {
                normalized[0]=types->types[i][0]=='P'?'C':types->types[i][0];
                normalized[1]=types->types[i][1];
                normalized[2]='?';
                normalized[3]='\0';
            }
            strcpy(satellite->values[i].code,normalized);
            if (!parse_value_field(line,field_index*16,1.0,satellite->values+i)) {
                set_error(converter,"line %lu: invalid value for %s %s",
                          converter->reader.line_number,satellite->id,
                          types->types[i]);
                free(line);
                free_satellite(satellite);
                return 0;
            }
            strcpy(satellite->values[i].code,normalized);
        }
        free(line);
        line=NULL;
    }
    return 1;
}

static int parse_v3_epoch(const char *line, gtime_t *time, int *flag,
                          int *satellite_count, double *clock_offset)
{
    double epoch[6],offset=0.0;
    int n;
    if (line[0]!='>') return 0;
    n=sscanf(line+1,"%lf %lf %lf %lf %lf %lf %d %d %lf",
             epoch,epoch+1,epoch+2,epoch+3,epoch+4,epoch+5,flag,
             satellite_count,&offset);
    if (n<8||*satellite_count<0) return 0;
    *time=epoch2time(epoch);
    *clock_offset=n>=9?offset:0.0;
    return time->time!=0;
}

static int parse_v2_epoch(converter_t *converter, const char *first_line,
                          gtime_t *time, int *flag, int *satellite_count,
                          double *clock_offset, char (**satellite_ids)[4])
{
    char (*ids)[4]=NULL;
    char *line=NULL;
    int i,status;
    size_t position;
    if (!parse_int_columns(first_line,28,1,flag)||
        !parse_int_columns(first_line,29,3,satellite_count)||
        *satellite_count<0) return 0;
    if (*flag>=2&&*flag<=5) {
        memset(time,0,sizeof(*time));
        return 1;
    }
    if (str2time(first_line,0,26,time)) return 0;
    *clock_offset=0.0;
    parse_double_columns(first_line,68,12,clock_offset);
    if (*satellite_count==0) return 1;
    ids=(char (*)[4])calloc((size_t)*satellite_count,sizeof(*ids));
    if (!ids) {
        set_error(converter,"out of memory reading RINEX 2 satellite list");
        return 0;
    }
    line=(char *)first_line;
    for (i=0;i<*satellite_count;i++) {
        if (i>0&&i%12==0) {
            if (line!=first_line) free(line);
            status=read_line(&converter->reader,&line);
            if (status<=0) {
                set_error(converter,"unexpected end of RINEX 2 satellite list");
                free(ids);
                return 0;
            }
        }
        position=32+(size_t)(i%12)*3;
        copy_columns(ids[i],sizeof(ids[i]),line,position,3);
        if (!ids[i][0]) {
            if (line!=first_line) free(line);
            set_error(converter,"line %lu: missing RINEX 2 satellite identifier",
                      converter->reader.line_number);
            free(ids);
            return 0;
        }
    }
    if (line!=first_line) free(line);
    *satellite_ids=ids;
    return 1;
}

static int string_buffer_reserve(string_buffer_t *buffer, size_t additional)
{
    size_t required,new_capacity;
    char *expanded;
    if (additional>((size_t)-1)-buffer->len-1) return 0;
    required=buffer->len+additional+1;
    if (required<=buffer->cap) return 1;
    new_capacity=buffer->cap?buffer->cap:256;
    while (new_capacity<required) {
        if (new_capacity>((size_t)-1)/2) return 0;
        new_capacity*=2;
    }
    expanded=(char *)realloc(buffer->data,new_capacity);
    if (!expanded) return 0;
    buffer->data=expanded;
    buffer->cap=new_capacity;
    return 1;
}

static int string_buffer_appendf(string_buffer_t *buffer, const char *format,...)
{
    va_list ap,copy;
    int needed;
    va_start(ap,format);
    va_copy(copy,ap);
    needed=vsnprintf(NULL,0,format,copy);
    va_end(copy);
    if (needed<0||!string_buffer_reserve(buffer,(size_t)needed)) {
        va_end(ap);
        return 0;
    }
    vsnprintf(buffer->data+buffer->len,buffer->cap-buffer->len,format,ap);
    va_end(ap);
    buffer->len+=(size_t)needed;
    return 1;
}

static void free_string_buffer(string_buffer_t *buffer)
{
    free(buffer->data);
    memset(buffer,0,sizeof(*buffer));
}

static int map_output_prn(char sys, int rinex_prn, int *range_prn)
{
    size_t i;
    for (i=0;i<sizeof(system_prn_maps)/sizeof(system_prn_maps[0]);i++) {
        const system_prn_map_t *mapping=system_prn_maps+i;
        if (mapping->sys!=sys) continue;
        if (rinex_prn<mapping->rinex_min||rinex_prn>mapping->rinex_max)
            return 0;
        *range_prn=rinex_prn+mapping->range_offset;
        return 1;
    }
    return 0;
}

static int qzss_l1cb_prn(int range_prn)
{
    switch (range_prn) {
        case 196: return 203;
        case 197: return 204;
        case 200: return 205;
        case 201: return 206;
        default: return 0;
    }
}

static int find_value(const satellite_obs_t *satellite, char type,
                      const char *signal)
{
    size_t i;
    for (i=0;i<satellite->n;i++) {
        if (satellite->values[i].code[0]==type&&
            satellite->values[i].code[1]==signal[0]&&
            satellite->values[i].code[2]==signal[1]) return (int)i;
    }
    return -1;
}

static int find_channel_value(const satellite_obs_t *satellite, char band)
{
    size_t i;
    for (i=0;i<satellite->n;i++) {
        if (satellite->values[i].code[0]=='X'&&
            satellite->values[i].code[1]==band) return (int)i;
    }
    return -1;
}

static void format_epoch(gtime_t time, char *text, size_t size)
{
    double epoch[6];
    time2epoch(time,epoch);
    snprintf(text,size,"%04.0f-%02.0f-%02.0fT%02.0f:%02.0f:%09.6f",
             epoch[0],epoch[1],epoch[2],epoch[3],epoch[4],epoch[5]);
}

static void diagnostic_item(converter_t *converter, gtime_t time,
                            const satellite_obs_t *satellite,
                            const rinex_value_t *value, const char *category,
                            const char *reason)
{
    char epoch[64];
    if (!converter->options->emit_warnings||!converter->diagnostic) return;
    format_epoch(time,epoch,sizeof(epoch));
    fprintf(converter->diagnostic,
            "rnx2range: %s: epoch=%s satellite=%s observation=%s reason=%s\n",
            category,epoch,satellite->id,value->code,reason);
}

static void mark_converted(converter_t *converter, const rinex_value_t *value)
{
    int type=value_type_index(value->code[0]);
    converter->report->converted_values++;
    if (type>=0) converter->report->converted_by_type[type]++;
}

static void count_skipped_code(converter_t *converter, char sys,
                               const char *code)
{
    size_t i,capacity;
    code_count_t *expanded;
    for (i=0;i<converter->code_count;i++) {
        if (converter->code_counts[i].sys==sys&&
            !strcmp(converter->code_counts[i].code,code)) {
            converter->code_counts[i].count++;
            return;
        }
    }
    if (converter->code_count==converter->code_capacity) {
        capacity=converter->code_capacity?converter->code_capacity*2:32;
        expanded=(code_count_t *)realloc(converter->code_counts,
                                         capacity*sizeof(*expanded));
        if (!expanded) return;
        converter->code_counts=expanded;
        converter->code_capacity=capacity;
    }
    converter->code_counts[converter->code_count].sys=sys;
    strcpy(converter->code_counts[converter->code_count].code,code);
    converter->code_counts[converter->code_count].count=1;
    converter->code_count++;
}

static void count_reason(converter_t *converter, const char *category,
                         const char *reason)
{
    size_t i,capacity;
    reason_count_t *expanded;
    for (i=0;i<converter->reason_count;i++) {
        if (!strcmp(converter->reason_counts[i].category,category)&&
            !strcmp(converter->reason_counts[i].reason,reason)) {
            converter->reason_counts[i].count++;
            return;
        }
    }
    if (converter->reason_count==converter->reason_capacity) {
        capacity=converter->reason_capacity?converter->reason_capacity*2:16;
        expanded=(reason_count_t *)realloc(converter->reason_counts,
                                           capacity*sizeof(*expanded));
        if (!expanded) return;
        converter->reason_counts=expanded;
        converter->reason_capacity=capacity;
    }
    snprintf(converter->reason_counts[converter->reason_count].category,
             sizeof(converter->reason_counts[converter->reason_count].category),
             "%s",category);
    snprintf(converter->reason_counts[converter->reason_count].reason,
             sizeof(converter->reason_counts[converter->reason_count].reason),
             "%s",reason);
    converter->reason_counts[converter->reason_count].count=1;
    converter->reason_count++;
}

static void mark_unsupported(converter_t *converter, gtime_t time,
                             const satellite_obs_t *satellite,
                             const rinex_value_t *value, const char *reason)
{
    int type=value_type_index(value->code[0]);
    converter->report->skipped_unsupported_values++;
    if (type>=0) converter->report->skipped_unsupported_by_type[type]++;
    converter->had_unsupported=1;
    count_skipped_code(converter,satellite->sys,value->code);
    count_reason(converter,"target-unsupported",reason);
    diagnostic_item(converter,time,satellite,value,"unsupported",reason);
}

static void mark_conversion_error(converter_t *converter, gtime_t time,
                                  const satellite_obs_t *satellite,
                                  const rinex_value_t *value,
                                  const char *reason)
{
    converter->report->conversion_errors++;
    converter->had_conversion_error=1;
    count_skipped_code(converter,satellite->sys,value->code);
    count_reason(converter,"conversion-error",reason);
    diagnostic_item(converter,time,satellite,value,"conversion-error",reason);
}

static lock_state_t *get_lock_state(converter_t *converter, char sys, int prn,
                                    const char *signal)
{
    size_t i;
    lock_state_t *expanded;
    for (i=0;i<converter->lock_count;i++) {
        lock_state_t *state=converter->locks+i;
        if (state->sys==sys&&state->prn==prn&&!strcmp(state->signal,signal))
            return state;
    }
    if (converter->lock_count==converter->lock_capacity) {
        size_t capacity=converter->lock_capacity?converter->lock_capacity*2:64;
        expanded=(lock_state_t *)realloc(converter->locks,capacity*sizeof(*expanded));
        if (!expanded) return NULL;
        converter->locks=expanded;
        converter->lock_capacity=capacity;
    }
    memset(converter->locks+converter->lock_count,0,sizeof(*converter->locks));
    converter->locks[converter->lock_count].sys=sys;
    converter->locks[converter->lock_count].prn=prn;
    strcpy(converter->locks[converter->lock_count].signal,signal);
    return converter->locks+converter->lock_count++;
}

static void reset_all_locks(converter_t *converter)
{
    size_t i;
    for (i=0;i<converter->lock_count;i++) converter->locks[i].active=0;
}

static double update_locktime(converter_t *converter, gtime_t time, char sys,
                              int prn, const char *signal, unsigned char lli)
{
    lock_state_t *state=get_lock_state(converter,sys,prn,signal);
    double difference;
    if (!state) return -1.0;
    difference=timediff(time,state->last_time);
    if (!state->active||(lli&1)||state->last_epoch+1!=converter->report->input_epochs||
        difference<0.0) state->locktime=0.0;
    else state->locktime+=difference;
    state->active=1;
    state->last_time=time;
    state->last_epoch=converter->report->input_epochs;
    return state->locktime;
}

static void deactivate_lock(converter_t *converter, char sys, int prn,
                            const char *signal)
{
    size_t i;
    for (i=0;i<converter->lock_count;i++) {
        lock_state_t *state=converter->locks+i;
        if (state->sys==sys&&state->prn==prn&&!strcmp(state->signal,signal)) {
            state->active=0;
            return;
        }
    }
}

static int signal_type_for_satellite(const converter_t *converter,
                                     const signal_map_t *mapping,
                                     int rinex_prn)
{
    int signal_type=mapping->signal_type;
    if (mapping->sys=='C'&&(signal_type==0||signal_type==1||signal_type==2)&&
        rinex_prn<=converter->options->bds_d2_max_prn) signal_type+=4;
    return signal_type;
}

static void count_source_values(converter_t *converter,
                                const satellite_obs_t *satellite)
{
    size_t i,j;
    converter->report->source_satellites++;
    for (i=0;i<satellite->n;i++) {
        const rinex_value_t *value=satellite->values+i;
        int type;
        if (!value->present) continue;
        converter->report->source_nonempty_values++;
        type=value_type_index(value->code[0]);
        if (type>=0) converter->report->source_nonempty_by_type[type]++;
        if (!strchr("CLDS",value->code[0])||strlen(value->code)<3) continue;
        for (j=0;j<i;j++) {
            const rinex_value_t *previous=satellite->values+j;
            if (previous->present&&strchr("CLDS",previous->code[0])&&
                previous->code[1]==value->code[1]&&
                previous->code[2]==value->code[2]) break;
        }
        if (j==i) converter->report->source_signal_records++;
    }
}

static void mark_present_indices(converter_t *converter, gtime_t time,
                                 const satellite_obs_t *satellite,
                                 const int indices[4], int *handled,
                                 int unsupported, const char *reason)
{
    int i,index;
    for (i=0;i<4;i++) {
        index=indices[i];
        if (index<0||!satellite->values[index].present) continue;
        handled[index]=1;
        if (unsupported)
            mark_unsupported(converter,time,satellite,satellite->values+index,reason);
        else
            mark_conversion_error(converter,time,satellite,satellite->values+index,reason);
    }
}

static int process_satellite(converter_t *converter, gtime_t time,
                             const satellite_obs_t *satellite,
                             string_buffer_t *records, size_t *record_count)
{
    int *handled;
    size_t map_index,i;
    int range_prn;
    count_source_values(converter,satellite);
    handled=(int *)calloc(satellite->n,sizeof(*handled));
    if (!handled) {
        set_error(converter,"out of memory processing satellite %s",satellite->id);
        return 0;
    }
    for (map_index=0;map_index<sizeof(signal_maps)/sizeof(signal_maps[0]);map_index++) {
        const signal_map_t *mapping=signal_maps+map_index;
        int indices[4],channel_index,has_any=0,glofreq=0,channel=0;
        int phase_locked,code_locked=1,parity_known=0,signal_type;
        unsigned int tracking_status;
        double psr,adr=0.0,doppler=0.0,cno=0.0,locktime=0.0;
        unsigned char lli=0;
        if (mapping->sys!=satellite->sys) continue;
        indices[0]=find_value(satellite,'C',mapping->signal);
        indices[1]=find_value(satellite,'L',mapping->signal);
        indices[2]=find_value(satellite,'D',mapping->signal);
        indices[3]=find_value(satellite,'S',mapping->signal);
        for (i=0;i<4;i++) {
            if (indices[i]>=0&&satellite->values[indices[i]].present) has_any=1;
        }
        if (!has_any) continue;
        if (indices[0]<0||!satellite->values[indices[0]].present) {
            mark_present_indices(converter,time,satellite,indices,handled,0,
                                 "RANGE requires pseudorange for this signal record");
            continue;
        }
        if (!map_output_prn(satellite->sys,satellite->rinex_prn,&range_prn)) {
            mark_present_indices(converter,time,satellite,indices,handled,1,
                                 "satellite is outside the OEM7 RANGE PRN/slot range");
            continue;
        }
        if (satellite->sys=='R') {
            int fcn=satellite->rinex_prn<100?
                    converter->header.glo_fcn[satellite->rinex_prn]:999;
            if (fcn==999) {
                mark_present_indices(converter,time,satellite,indices,handled,0,
                                     "GLONASS frequency channel is missing");
                continue;
            }
            glofreq=fcn+7;
        }
        else if (satellite->sys=='J'&&!strcmp(mapping->signal,"1E")) {
            glofreq=qzss_l1cb_prn(range_prn);
            if (!glofreq) {
                mark_present_indices(converter,time,satellite,indices,handled,1,
                                     "no QZSS L1C/B PRN is defined for this satellite");
                continue;
            }
        }
        psr=satellite->values[indices[0]].value;
        if (!(psr>0.0)) {
            mark_present_indices(converter,time,satellite,indices,handled,0,
                                 "RANGE pseudorange must be positive");
            continue;
        }
        for (i=0;i<4;i++) if (indices[i]>=0&&satellite->values[indices[i]].present)
            handled[indices[i]]=1;

        phase_locked=indices[1]>=0&&satellite->values[indices[1]].present;
        if (phase_locked) {
            const rinex_value_t *phase=satellite->values+indices[1];
            lli=phase->lli;
            adr=-phase->value+mapping->phase_shift;
            locktime=update_locktime(converter,time,satellite->sys,
                                     satellite->rinex_prn,mapping->signal,lli);
            if (locktime<0.0) {
                free(handled);
                set_error(converter,"out of memory tracking lock time");
                return 0;
            }
            parity_known=(lli&2)==0;
        }
        else {
            deactivate_lock(converter,satellite->sys,satellite->rinex_prn,
                            mapping->signal);
        }
        if (indices[2]>=0&&satellite->values[indices[2]].present)
            doppler=satellite->values[indices[2]].value;
        if (indices[3]>=0&&satellite->values[indices[3]].present) {
            if (converter->header.snr_dbhz||converter->options->assume_snr_dbhz) {
                cno=satellite->values[indices[3]].value;
                mark_converted(converter,satellite->values+indices[3]);
            }
            else {
                mark_conversion_error(converter,time,satellite,
                                      satellite->values+indices[3],
                                      "signal-strength unit is not DBHZ");
            }
        }

        channel_index=find_channel_value(satellite,mapping->signal[0]);
        if (channel_index>=0&&satellite->values[channel_index].present) {
            const rinex_value_t *channel_value=satellite->values+channel_index;
            double rounded=floor(channel_value->value+0.5);
            if (fabs(channel_value->value-rounded)>1E-6||rounded<1.0||rounded>32.0) {
                if (!handled[channel_index]) {
                    mark_conversion_error(converter,time,satellite,channel_value,
                                          "RINEX receiver channel must be an integer from 1 to 32");
                    handled[channel_index]=1;
                }
                channel=0;
                converter->report->synthetic_channel++;
            }
            else {
                channel=(int)rounded-1;
                if (!handled[channel_index]) {
                    mark_converted(converter,channel_value);
                    handled[channel_index]=1;
                }
            }
        }
        else {
            channel=0;
            converter->report->synthetic_channel++;
        }

        signal_type=signal_type_for_satellite(converter,mapping,
                                              satellite->rinex_prn);
        if (satellite->sys=='C'&&(mapping->signal_type==0||
            mapping->signal_type==1||mapping->signal_type==2))
            converter->report->synthetic_bds_data_type++;
        tracking_status=(unsigned int)(phase_locked?4:7);
        tracking_status|=((unsigned int)channel&0x1FU)<<5;
        if (phase_locked) tracking_status|=1U<<10;
        if (parity_known) tracking_status|=1U<<11;
        if (code_locked) tracking_status|=1U<<12;
        tracking_status|=((unsigned int)mapping->system_bits&7U)<<16;
        tracking_status|=((unsigned int)signal_type&0x1FU)<<21;

        if (!string_buffer_appendf(records,
                ",%d,%d,%.3f,%.3f,%.6f,%.3f,%.3f,%.1f,%.3f,%08x",
                range_prn,glofreq,psr,converter->options->psr_sigma,adr,
                phase_locked?converter->options->adr_sigma:0.0,doppler,cno,
                locktime,tracking_status)) {
            free(handled);
            set_error(converter,"out of memory encoding RANGEA record");
            return 0;
        }
        mark_converted(converter,satellite->values+indices[0]);
        if (phase_locked) mark_converted(converter,satellite->values+indices[1]);
        if (indices[2]>=0&&satellite->values[indices[2]].present)
            mark_converted(converter,satellite->values+indices[2]);
        converter->report->synthetic_psr_sigma++;
        converter->report->synthetic_adr_sigma++;
        converter->report->synthetic_locktime++;
        converter->report->synthetic_half_cycle++;
        if (!phase_locked) converter->report->synthetic_adr++;
        if (indices[2]<0||!satellite->values[indices[2]].present)
            converter->report->synthetic_doppler++;
        if (indices[3]<0||!satellite->values[indices[3]].present||
            (!converter->header.snr_dbhz&&!converter->options->assume_snr_dbhz))
            converter->report->synthetic_cno++;
        converter->report->synthetic_tracking_state++;
        converter->report->emitted_range_records++;
        (*record_count)++;
    }
    for (i=0;i<satellite->n;i++) {
        const rinex_value_t *value=satellite->values+i;
        if (!value->present||handled[i]) continue;
        mark_unsupported(converter,time,satellite,value,
                         find_signal_map(satellite->sys,value->code+1)?
                         "observation field cannot be represented in this RANGE record":
                         "constellation/signal has no OEM7 RANGE RINEX mapping");
    }
    free(handled);
    return 1;
}

static void rounded_week_tow(gtime_t time, int *week, double *tow)
{
    *tow=time2gpst(time,week);
    *tow=floor(*tow*1000.0+0.5)/1000.0;
    if (*tow>=604800.0) {
        *tow-=604800.0;
        (*week)++;
    }
    else if (*tow<0.0) {
        *tow+=604800.0;
        (*week)--;
    }
}

static int write_rangea_message(converter_t *converter, FILE *output,
                                gtime_t time, const string_buffer_t *records,
                                size_t record_count)
{
    string_buffer_t payload={0};
    unsigned int crc;
    int week;
    double tow;
    rounded_week_tow(time,&week,&tow);
    if (week<0) {
        set_error(converter,"RANGEA cannot represent an epoch before GPS time");
        return 0;
    }
    if (!string_buffer_appendf(&payload,
            "RANGEA,%s,0,%.1f,%s,%d,%.3f,%08x,%x,%u;%lu%s",
            converter->options->port,converter->options->idle_time,
            converter->options->time_status,week,tow,
            converter->options->receiver_status,converter->options->reserved,
            converter->options->software_version,(unsigned long)record_count,
            records->data?records->data:"")) {
        set_error(converter,"out of memory encoding RANGEA message");
        return 0;
    }
    if (payload.len>(size_t)INT_MAX) {
        set_error(converter,"RANGEA message is too large to checksum");
        free_string_buffer(&payload);
        return 0;
    }
    crc=rtk_crc32((const unsigned char *)payload.data,(int)payload.len);
    if (fprintf(output,"#%s*%08x\r\n",payload.data,crc)<0) {
        set_error(converter,"failed to write RANGEA output");
        free_string_buffer(&payload);
        return 0;
    }
    converter->report->output_messages++;
    converter->report->synthetic_header_port++;
    converter->report->synthetic_header_idle_time++;
    converter->report->synthetic_header_time_status++;
    converter->report->synthetic_header_receiver_status++;
    converter->report->synthetic_header_reserved++;
    converter->report->synthetic_header_software_version++;
    if (!converter->report->have_time_span) {
        converter->report->first_week=week;
        converter->report->first_tow=tow;
        converter->report->have_time_span=1;
    }
    converter->report->last_week=week;
    converter->report->last_tow=tow;
    free_string_buffer(&payload);
    return 1;
}

static int skip_lines(converter_t *converter, int count)
{
    int i,status;
    char *line=NULL;
    for (i=0;i<count;i++) {
        status=read_line(&converter->reader,&line);
        if (status<=0) {
            set_error(converter,"unexpected end of RINEX event records");
            return 0;
        }
        free(line);
        line=NULL;
    }
    return 1;
}

static void free_satellite_array(satellite_obs_t *satellites, int count)
{
    int i;
    if (!satellites) return;
    for (i=0;i<count;i++) free_satellite(satellites+i);
    free(satellites);
}

static int convert_body(converter_t *converter, FILE *output)
{
    int status;
    char *line=NULL;
    while ((status=read_line(&converter->reader,&line))>0) {
        gtime_t time={0},gpst;
        int flag=0,satellite_count=0,satellite_index;
        double clock_offset=0.0;
        char (*satellite_ids)[4]=NULL;
        satellite_obs_t *satellites=NULL;
        string_buffer_t records={0};
        size_t record_count=0;
        if (!line[0]) { free(line); line=NULL; continue; }
        if (converter->header.version>=3.0) {
            if (!parse_v3_epoch(line,&time,&flag,&satellite_count,&clock_offset)) {
                set_error(converter,"line %lu: invalid RINEX observation epoch",
                          converter->reader.line_number);
                free(line);
                return 0;
            }
        }
        else if (!parse_v2_epoch(converter,line,&time,&flag,&satellite_count,
                                 &clock_offset,&satellite_ids)) {
            if (!converter->error_message||!converter->error_message[0])
                set_error(converter,"line %lu: invalid RINEX 2 observation epoch",
                          converter->reader.line_number);
            free(line);
            return 0;
        }
        free(line);
        line=NULL;

        if (flag>=2&&flag<=5) {
            free(satellite_ids);
            reset_all_locks(converter);
            if (!skip_lines(converter,satellite_count)) return 0;
            continue;
        }
        if (flag==6) {
            reset_all_locks(converter);
            if (converter->header.version>=3.0) {
                if (!skip_lines(converter,satellite_count)) return 0;
            }
            else {
                for (satellite_index=0;satellite_index<satellite_count;satellite_index++) {
                    satellite_obs_t satellite;
                    if (!read_v2_satellite(converter,satellite_ids[satellite_index],
                                           &satellite)) {
                        free(satellite_ids);
                        return 0;
                    }
                    free_satellite(&satellite);
                }
            }
            free(satellite_ids);
            continue;
        }
        if (flag!=0&&flag!=1) {
            free(satellite_ids);
            set_error(converter,"unsupported RINEX epoch flag %d",flag);
            return 0;
        }
        gpst=to_gpst(time,converter->header.time_system);
        if (satellite_count>0&&
            !(satellites=(satellite_obs_t *)calloc((size_t)satellite_count,
                                                   sizeof(*satellites)))) {
            free(satellite_ids);
            set_error(converter,"out of memory buffering observation epoch");
            return 0;
        }

        for (satellite_index=0;satellite_index<satellite_count;satellite_index++) {
            satellite_obs_t *satellite=satellites+satellite_index;
            if (converter->header.version>=3.0) {
                status=read_line(&converter->reader,&line);
                if (status<=0) {
                    char epoch_text[64];
                    format_epoch(gpst,epoch_text,sizeof(epoch_text));
                    set_error(converter,
                              "line %lu: incomplete epoch %s: expected %d satellite records, found %d",
                              converter->reader.line_number,epoch_text,
                              satellite_count,satellite_index);
                    free(satellite_ids);
                    free_satellite_array(satellites,satellite_count);
                    return 0;
                }
                if (!parse_v3_satellite(converter,line,satellite)) {
                    free(line);
                    free(satellite_ids);
                    free_satellite_array(satellites,satellite_count);
                    return 0;
                }
                free(line);
                line=NULL;
            }
            else if (!read_v2_satellite(converter,satellite_ids[satellite_index],
                                        satellite)) {
                free(satellite_ids);
                free_satellite_array(satellites,satellite_count);
                return 0;
            }
        }
        free(satellite_ids);
        if (flag==1) reset_all_locks(converter);
        converter->report->input_epochs++;
        for (satellite_index=0;satellite_index<satellite_count;satellite_index++) {
            if (!process_satellite(converter,gpst,satellites+satellite_index,
                                   &records,&record_count)) {
                free_satellite_array(satellites,satellite_count);
                free_string_buffer(&records);
                return 0;
            }
        }
        free_satellite_array(satellites,satellite_count);
        if (!write_rangea_message(converter,output,gpst,&records,record_count)) {
            free_string_buffer(&records);
            return 0;
        }
        free_string_buffer(&records);
        (void)clock_offset;
    }
    if (status<0) {
        set_error(converter,"out of memory reading RINEX body");
        return 0;
    }
    return 1;
}

static int safe_header_word(const char *text)
{
    const unsigned char *p=(const unsigned char *)text;
    if (!*p) return 0;
    for (;*p;p++) {
        if (*p<33||*p>126||strchr("#,;*",*p)) return 0;
    }
    return 1;
}

static void print_detailed_skip_summary(FILE *stream,
                                        const converter_t *converter)
{
    size_t i;
    if (!stream) return;
    fprintf(stream,"skipped_records_by_reason=");
    for (i=0;i<converter->reason_count;i++) {
        const reason_count_t *entry=converter->reason_counts+i;
        fprintf(stream,"%s%s:%s:%lu",i?";":"",entry->category,
                entry->reason,entry->count);
    }
    fprintf(stream,"\n");
    fprintf(stream,"skipped_values_by_observation_code=");
    for (i=0;i<converter->code_count;i++) {
        const code_count_t *entry=converter->code_counts+i;
        fprintf(stream,"%s%c:%s:%lu",i?",":"",entry->sys,entry->code,
                entry->count);
    }
    fprintf(stream,"\n");
}

void rnxrange_default_options(rnxrange_options_t *options)
{
    if (!options) return;
    memset(options,0,sizeof(*options));
    strcpy(options->port,"COM1");
    strcpy(options->time_status,"UNKNOWN");
    options->idle_time=0.0;
    options->receiver_status=0;
    options->reserved=0;
    options->software_version=0;
    options->psr_sigma=0.500;
    options->adr_sigma=0.050;
    options->strict=1;
    options->fail_on_unsupported=0;
    options->assume_snr_dbhz=0;
    options->emit_warnings=1;
    options->bds_d2_max_prn=5;
}

int rnxrange_convert(FILE *input, FILE *output,
                     const rnxrange_options_t *options,
                     rnxrange_report_t *report, FILE *diagnostic,
                     char *error_message, size_t error_message_size)
{
    converter_t converter;
    rnxrange_options_t defaults;
    rnxrange_report_t local_report;
    int status;
    if (error_message&&error_message_size) error_message[0]='\0';
    if (!input||!output) {
        if (error_message&&error_message_size)
            snprintf(error_message,error_message_size,"input and output streams are required");
        return 0;
    }
    if (!options) {
        rnxrange_default_options(&defaults);
        options=&defaults;
    }
    if (!report) report=&local_report;
    memset(report,0,sizeof(*report));
    if (!safe_header_word(options->port)||!safe_header_word(options->time_status)||
        !isfinite(options->idle_time)||options->idle_time<0.0||
        options->idle_time>100.0||!isfinite(options->psr_sigma)||
        !isfinite(options->adr_sigma)||!(options->psr_sigma>0.0)||
        !(options->adr_sigma>0.0)||
        options->software_version>65535U||options->bds_d2_max_prn<0||
        options->bds_d2_max_prn>63) {
        if (error_message&&error_message_size)
            snprintf(error_message,error_message_size,"invalid RANGEA conversion options");
        return 0;
    }
    memset(&converter,0,sizeof(converter));
    converter.reader.fp=input;
    converter.options=options;
    converter.report=report;
    converter.diagnostic=diagnostic;
    converter.error_message=error_message;
    converter.error_message_size=error_message_size;
    status=parse_header(&converter)&&convert_body(&converter,output);
    if (status&&fflush(output)==EOF) {
        set_error(&converter,"failed to flush RANGEA output");
        status=0;
    }
    if (status&&options->strict&&converter.had_conversion_error) {
        set_error(&converter,
                  "strict conversion failed: %lu supported observation values could not be converted",
                  report->conversion_errors);
        status=0;
    }
    if (status&&options->fail_on_unsupported&&converter.had_unsupported) {
        set_error(&converter,
                  "conversion failed: %lu observation values are not supported by OEM7 RANGE",
                  report->skipped_unsupported_values);
        status=0;
    }
    print_detailed_skip_summary(diagnostic,&converter);
    free_header(&converter.header);
    free(converter.locks);
    free(converter.code_counts);
    free(converter.reason_counts);
    return status;
}

int rnxrange_convert_file(const char *input_path, const char *output_path,
                          const rnxrange_options_t *options,
                          rnxrange_report_t *report, FILE *diagnostic,
                          char *error_message, size_t error_message_size)
{
    FILE *input=NULL,*output=NULL;
    int status;
    if (!input_path||!output_path) {
        if (error_message&&error_message_size)
            snprintf(error_message,error_message_size,"input and output paths are required");
        return 0;
    }
    if (strcmp(input_path,"-")&&strcmp(output_path,"-")&&
        !strcmp(input_path,output_path)) {
        if (error_message&&error_message_size)
            snprintf(error_message,error_message_size,"input and output paths must differ");
        return 0;
    }
    input=!strcmp(input_path,"-")?stdin:fopen(input_path,"rb");
    if (!input) {
        if (error_message&&error_message_size)
            snprintf(error_message,error_message_size,"cannot open input %s: %s",
                     input_path,strerror(errno));
        return 0;
    }
    output=!strcmp(output_path,"-")?stdout:fopen(output_path,"wb");
    if (!output) {
        if (input!=stdin) fclose(input);
        if (error_message&&error_message_size)
            snprintf(error_message,error_message_size,"cannot open output %s: %s",
                     output_path,strerror(errno));
        return 0;
    }
    status=rnxrange_convert(input,output,options,report,diagnostic,
                            error_message,error_message_size);
    if (input!=stdin&&fclose(input)!=0&&status) {
        if (error_message&&error_message_size)
            snprintf(error_message,error_message_size,"failed to close input %s",input_path);
        status=0;
    }
    if (output!=stdout&&fclose(output)!=0&&status) {
        if (error_message&&error_message_size)
            snprintf(error_message,error_message_size,"failed to close output %s",output_path);
        status=0;
    }
    return status;
}

void rnxrange_print_report(FILE *stream, const rnxrange_report_t *report)
{
    int i;
    if (!stream||!report) return;
    fprintf(stream,"input_epochs=%lu\n",report->input_epochs);
    fprintf(stream,"output_messages=%lu\n",report->output_messages);
    fprintf(stream,"source_satellites=%lu\n",report->source_satellites);
    fprintf(stream,"source_signal_records=%lu\n",report->source_signal_records);
    fprintf(stream,"source_nonempty_values=%lu\n",report->source_nonempty_values);
    fprintf(stream,"source_nonempty_values_by_type=");
    for (i=0;i<RNXRANGE_VALUE_TYPES;i++)
        fprintf(stream,"%s%c:%lu",i?",":"",value_type_names[i],
                report->source_nonempty_by_type[i]);
    fprintf(stream,"\n");
    fprintf(stream,"emitted_range_records=%lu\n",report->emitted_range_records);
    fprintf(stream,"converted_values=%lu\n",report->converted_values);
    fprintf(stream,"converted_values_by_type=");
    for (i=0;i<RNXRANGE_VALUE_TYPES;i++)
        fprintf(stream,"%s%c:%lu",i?",":"",value_type_names[i],
                report->converted_by_type[i]);
    fprintf(stream,"\n");
    fprintf(stream,"skipped_unsupported_values=%lu\n",
            report->skipped_unsupported_values);
    fprintf(stream,"skipped_unsupported_by_type=");
    for (i=0;i<RNXRANGE_VALUE_TYPES;i++)
        fprintf(stream,"%s%c:%lu",i?",":"",value_type_names[i],
                report->skipped_unsupported_by_type[i]);
    fprintf(stream,"\n");
    fprintf(stream,"conversion_errors=%lu\n",report->conversion_errors);
    fprintf(stream,"synthetic_fields_by_name=adr_placeholder:%lu,doppler_placeholder:%lu,cno_placeholder:%lu,psr_sigma:%lu,adr_sigma:%lu,sv_channel:%lu,locktime:%lu,half_cycle_added:%lu,tracking_state:%lu,bds_data_type:%lu,header_port:%lu,header_idle_time:%lu,header_time_status:%lu,header_receiver_status:%lu,header_reserved:%lu,header_software_version:%lu\n",
            report->synthetic_adr,report->synthetic_doppler,
            report->synthetic_cno,report->synthetic_psr_sigma,
            report->synthetic_adr_sigma,report->synthetic_channel,
            report->synthetic_locktime,report->synthetic_half_cycle,
            report->synthetic_tracking_state,
            report->synthetic_bds_data_type,report->synthetic_header_port,
            report->synthetic_header_idle_time,
            report->synthetic_header_time_status,
            report->synthetic_header_receiver_status,
            report->synthetic_header_reserved,
            report->synthetic_header_software_version);
    if (report->have_time_span) {
        fprintf(stream,"first_gps_time=%d,%.3f\n",report->first_week,
                report->first_tow);
        fprintf(stream,"last_gps_time=%d,%.3f\n",report->last_week,
                report->last_tow);
    }
    fprintf(stream,"conservation=%lu=%lu+%lu+%lu\n",
            report->source_nonempty_values,report->converted_values,
            report->skipped_unsupported_values,report->conversion_errors);
}
