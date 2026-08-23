#include "rnxrange.h"
#include "rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition); \
        return 0; \
    } \
} while (0)

typedef struct {
    int prn;
    int glofreq;
    double psr;
    double psr_sigma;
    double adr;
    double adr_sigma;
    double doppler;
    double cno;
    double locktime;
    unsigned int status;
} parsed_record_t;

static void put_text(char *line, int position, int width, const char *text)
{
    int length=(int)strlen(text);
    if (length>width) length=width;
    memcpy(line+position,text,(size_t)length);
}

static void put_int(char *line, int position, int width, int value)
{
    char format[16],field[64];
    snprintf(format,sizeof(format),"%%%dd",width);
    snprintf(field,sizeof(field),format,value);
    memcpy(line+position,field,(size_t)width);
}

static void header_line(FILE *file, const char *content, const char *label)
{
    fprintf(file,"%-60.60s%-20s\n",content,label);
}

static void version_header(FILE *file, double version, char system)
{
    char line[61],version_text[16];
    memset(line,' ',60);
    line[60]='\0';
    snprintf(version_text,sizeof(version_text),"%9.2f",version);
    put_text(line,0,9,version_text);
    line[20]='O';
    line[40]=system;
    header_line(file,line,"RINEX VERSION / TYPE");
}

static void observation_types(FILE *file, char system,
                              const char (*types)[4], int count)
{
    int index=0,slot;
    while (index<count) {
        char line[61];
        memset(line,' ',60);
        line[60]='\0';
        if (index==0) {
            line[0]=system;
            put_int(line,3,3,count);
        }
        for (slot=0;slot<13&&index<count;slot++,index++)
            put_text(line,7+slot*4,3,types[index]);
        header_line(file,line,"SYS / # / OBS TYPES");
    }
}

static void scale_factor(FILE *file, char system, int factor, const char *type)
{
    char line[61];
    memset(line,' ',60);
    line[60]='\0';
    line[0]=system;
    put_int(line,2,4,factor);
    put_int(line,8,2,1);
    put_text(line,11,3,type);
    header_line(file,line,"SYS / SCALE FACTOR");
}

static void finish_header_time(FILE *file, int dbhz, int glonass_fcn,
                               const char *time_system)
{
    char line[61];
    if (dbhz) header_line(file,"DBHZ","SIGNAL STRENGTH UNIT");
    if (glonass_fcn!=999) {
        snprintf(line,sizeof(line),"  1 R01 %2d",glonass_fcn);
        header_line(file,line,"GLONASS SLOT / FRQ #");
    }
    memset(line,' ',60);
    line[60]='\0';
    put_text(line,48,3,time_system);
    header_line(file,line,"TIME OF FIRST OBS");
    header_line(file,"","END OF HEADER");
}

static void finish_header(FILE *file, int dbhz, int glonass_fcn)
{
    finish_header_time(file,dbhz,glonass_fcn,"GPS");
}

static void observation_field(FILE *file, int present, double value,
                              int lli, int ssi)
{
    if (!present) fprintf(file,"                ");
    else fprintf(file,"%14.3f%d%d",value,lli,ssi);
}

static void satellite_line(FILE *file, const char *id, int count,
                           const int *present, const double *values,
                           const int *lli)
{
    int i;
    fprintf(file,"%s",id);
    for (i=0;i<count;i++)
        observation_field(file,present[i],values[i],lli?lli[i]:0,7);
    fprintf(file,"\n");
}

static unsigned int reference_crc32(const unsigned char *data, size_t length)
{
    unsigned int crc=0;
    size_t i;
    int bit;
    for (i=0;i<length;i++) {
        crc^=data[i];
        for (bit=0;bit<8;bit++)
            crc=(crc&1U)?(crc>>1)^0xEDB88320U:crc>>1;
    }
    return crc;
}

static int parse_range_line(char *line, parsed_record_t *records,
                            int maximum, int *record_count)
{
    char *star,*semicolon,*body,*token,*end;
    unsigned int actual_crc,expected_crc;
    int index=0,field=0;
    size_t length=strlen(line),payload_length;
    if (length<12||line[length-2]!='\r'||line[length-1]!='\n'||line[0]!='#')
        return 0;
    star=strrchr(line,'*');
    if (!star||strlen(star+1)<10) return 0;
    expected_crc=(unsigned int)strtoul(star+1,&end,16);
    if (end!=star+9) return 0;
    payload_length=(size_t)(star-(line+1));
    actual_crc=reference_crc32((const unsigned char *)line+1,payload_length);
    if (actual_crc!=expected_crc) return 0;
    semicolon=(char *)memchr(line,';',payload_length+1);
    if (!semicolon) return 0;
    *star='\0';
    body=semicolon+1;
    token=strtok(body,",");
    if (!token) return 0;
    *record_count=atoi(token);
    if (*record_count<0||*record_count>maximum) return 0;
    while ((token=strtok(NULL,","))!=NULL) {
        parsed_record_t *record;
        if (index>=*record_count) return 0;
        record=records+index;
        switch (field) {
            case 0: record->prn=atoi(token); break;
            case 1: record->glofreq=atoi(token); break;
            case 2: record->psr=strtod(token,NULL); break;
            case 3: record->psr_sigma=strtod(token,NULL); break;
            case 4: record->adr=strtod(token,NULL); break;
            case 5: record->adr_sigma=strtod(token,NULL); break;
            case 6: record->doppler=strtod(token,NULL); break;
            case 7: record->cno=strtod(token,NULL); break;
            case 8: record->locktime=strtod(token,NULL); break;
            case 9:
                record->status=(unsigned int)strtoul(token,NULL,16);
                index++;
                break;
        }
        field=(field+1)%10;
    }
    return index==*record_count&&field==0;
}

static int run_stream_conversion(FILE *input, FILE *output,
                                 rnxrange_options_t *options,
                                 rnxrange_report_t *report, char *error)
{
    FILE *diagnostic=tmpfile();
    int status;
    CHECK(diagnostic!=NULL);
    rewind(input);
    status=rnxrange_convert(input,output,options,report,diagnostic,error,512);
    fclose(diagnostic);
    return status;
}

static int test_mixed_mapping_and_crc(void)
{
    static const char gtypes[][4]={"C1C","L1C","D1C","S1C","C1L","L1L","C1N","X1"};
    static const char rtypes[][4]={"C2P","L2P","D2P","S2P"};
    static const char ctypes[][4]={"C5P","L5P","D5P","S5P"};
    static const char itypes[][4]={"C5A","L5A","D5A","S5A"};
    static const char jtypes[][4]={"C1E","L1E"};
    int gp[]={1,1,1,1,1,1,1,1},rp[]={1,1,1,1},cp[]={1,1,1,1};
    int ip[]={1,1,1,1},jp[]={1,1};
    int glli[]={0,0,0,0,0,0,0,0};
    double gv[]={22000000,1000,-100,45,22000010,200,22000020,5};
    double rv[]={23000000,300,-20,40};
    double cv[]={24000000,400,30,42};
    double iv[]={25000000,500,40,43};
    double jv[]={26000000,600};
    rnxrange_options_t options;
    rnxrange_report_t report;
    parsed_record_t records[16];
    FILE *input=tmpfile(),*output=tmpfile();
    char line[32768],error[512];
    int count;
    CHECK(input&&output);
    version_header(input,4.02,'M');
    observation_types(input,'G',gtypes,8);
    observation_types(input,'R',rtypes,4);
    observation_types(input,'C',ctypes,4);
    observation_types(input,'I',itypes,4);
    observation_types(input,'J',jtypes,2);
    header_line(input,"G L1C  0.25000","SYS / PHASE SHIFT");
    scale_factor(input,'G',10,"L1C");
    finish_header(input,1,-4);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0  5\n");
    satellite_line(input,"G01",8,gp,gv,glli);
    satellite_line(input,"R01",4,rp,rv,NULL);
    satellite_line(input,"C06",4,cp,cv,NULL);
    satellite_line(input,"I01",4,ip,iv,NULL);
    satellite_line(input,"J04",2,jp,jv,NULL);
    fflush(input);

    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.input_epochs==1);
    CHECK(report.source_signal_records==7);
    CHECK(report.source_nonempty_values==22);
    CHECK(report.converted_values==21);
    CHECK(report.skipped_unsupported_values==1);
    CHECK(report.conversion_errors==0);
    CHECK(report.emitted_range_records==6);
    CHECK(report.source_nonempty_values==report.converted_values+
          report.skipped_unsupported_values+report.conversion_errors);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(strncmp(line,"#RANGEA,COM1,0,0.0,FINE,",
                  sizeof("#RANGEA,COM1,0,0.0,FINE,")-1)==0);
    CHECK(parse_range_line(line,records,16,&count));
    CHECK(count==6);
    CHECK(fabs(records[0].adr-(-100.0))<1E-9); /* L1C scaled by 10 */
    CHECK(((records[0].status>>5)&0x1F)==4);    /* X1 channel 5 -> bits value 4 */
    CHECK(fabs(records[1].adr-(-199.75))<1E-9);
    CHECK(records[2].prn==38&&records[2].glofreq==3);
    CHECK(((records[2].status>>16)&7)==1);
    CHECK(((records[2].status>>21)&0x1F)==5);
    CHECK(((records[3].status>>21)&0x1F)==9);
    CHECK(((records[4].status>>16)&7)==6);
    CHECK(records[5].prn==196&&records[5].glofreq==203);
    CHECK(fgets(line,sizeof(line),output)==NULL);
    fclose(input);
    fclose(output);
    return 1;
}

static void v2_type_header(FILE *file, const char (*types)[3], int count)
{
    char line[61];
    int i;
    memset(line,' ',60);
    line[60]='\0';
    put_int(line,0,6,count);
    for (i=0;i<count;i++) put_text(line,10+i*6,2,types[i]);
    header_line(file,line,"# / TYPES OF OBSERV");
}

static int test_rinex2_mapping(void)
{
    static const char types[][3]={"C1","L1","D1","S1","P2","L2","D2","S2"};
    double values[]={22000000,1000,-10,45,22000005,800,-8,42};
    int present[]={1,1,1,1,1,1,1,1};
    rnxrange_options_t options;
    rnxrange_report_t report;
    parsed_record_t records[4];
    FILE *input=tmpfile(),*output=tmpfile();
    char epoch[81],line[4096],error[512];
    int i,count;
    CHECK(input&&output);
    version_header(input,2.11,'G');
    v2_type_header(input,types,8);
    finish_header(input,0,999);
    memset(epoch,' ',80);
    epoch[80]='\0';
    put_text(epoch,0,26," 24  1  1  0  0  0.0000000");
    epoch[28]='0';
    put_int(epoch,29,3,1);
    put_text(epoch,32,3,"G01");
    fprintf(input,"%s\n",epoch);
    for (i=0;i<5;i++) observation_field(input,present[i],values[i],0,7);
    fprintf(input,"\n");
    for (;i<8;i++) observation_field(input,present[i],values[i],0,7);
    fprintf(input,"\n");
    fflush(input);
    rnxrange_default_options(&options);
    options.assume_snr_dbhz=1;
    options.emit_warnings=0;
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.source_nonempty_values==8&&report.converted_values==8);
    CHECK(report.emitted_range_records==2);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(parse_range_line(line,records,4,&count)&&count==2);
    CHECK(((records[0].status>>21)&0x1F)==0);
    CHECK(((records[1].status>>21)&0x1F)==9);
    CHECK(fabs(records[1].adr+800.0)<1E-9);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_dynamic_capacity(void)
{
    static const char one_type[][4]={"C1C"};
    static const char valid_signals[][3]={"1C","1A","1B","1X","1Z",
        "5I","5Q","5X","7I","7Q","7X","8I","8Q","8X",
        "6A","6B","6C","6X"};
    static const char measurement_types[]="CLDS";
    char many_types[70][4],id[4],line[32768],error[512];
    int present[70],lli[70],count,i;
    double values[70];
    rnxrange_options_t options;
    rnxrange_report_t report;
    parsed_record_t records[80];
    FILE *input=tmpfile(),*output=tmpfile();
    CHECK(input&&output);
    version_header(input,3.05,'M');
    observation_types(input,'G',one_type,1);
    observation_types(input,'E',one_type,1);
    observation_types(input,'J',one_type,1);
    finish_header(input,0,999);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0 70\n");
    present[0]=1; values[0]=22000000; lli[0]=0;
    for (i=1;i<=32;i++) {
        snprintf(id,sizeof(id),"G%02d",i);
        satellite_line(input,id,1,present,values,lli);
    }
    for (i=1;i<=36;i++) {
        snprintf(id,sizeof(id),"E%02d",i);
        satellite_line(input,id,1,present,values,lli);
    }
    satellite_line(input,"J01",1,present,values,lli);
    satellite_line(input,"J02",1,present,values,lli);
    fflush(input);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.source_satellites==70&&report.emitted_range_records==70);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(parse_range_line(line,records,80,&count)&&count==70);
    fclose(input);
    fclose(output);

    input=tmpfile(); output=tmpfile();
    CHECK(input&&output);
    for (i=0;i<70;i++) {
        many_types[i][0]=measurement_types[i%4];
        many_types[i][1]=valid_signals[i/4][0];
        many_types[i][2]=valid_signals[i/4][1];
        many_types[i][3]='\0';
        present[i]=0; values[i]=0; lli[i]=0;
    }
    present[0]=1; values[0]=22000000; lli[0]=0;
    version_header(input,3.05,'E');
    observation_types(input,'E',(const char (*)[4])many_types,70);
    finish_header(input,0,999);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0  1\n");
    satellite_line(input,"E01",70,present,values,lli);
    fflush(input);
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.source_nonempty_values==1&&report.emitted_range_records==1);
    fclose(input);
    fclose(output);
    return 1;
}

static void fill_signal_values(int signal_count, int *present, double *values,
                               int *lli)
{
    int signal;
    for (signal=0;signal<signal_count;signal++) {
        present[signal*2]=present[signal*2+1]=1;
        values[signal*2]=22000000.0+signal;
        values[signal*2+1]=1000.0+signal;
        lli[signal*2]=lli[signal*2+1]=0;
    }
}

static int test_all_official_signal_mappings(void)
{
    static const char gtypes[][4]={"C1C","L1C","C1L","L1L","C2S","L2S",
        "C2P","L2P","C2W","L2W","C5Q","L5Q"};
    static const char rtypes[][4]={"C1C","L1C","C2C","L2C","C2P","L2P",
        "C3Q","L3Q"};
    static const char stypes[][4]={"C1C","L1C","C5I","L5I"};
    static const char etypes[][4]={"C1C","L1C","C5Q","L5Q","C7Q","L7Q",
        "C8Q","L8Q","C6B","L6B","C6C","L6C"};
    static const char ctypes[][4]={"C2I","L2I","C1P","L1P","C7I","L7I",
        "C5P","L5P","C7D","L7D","C6I","L6I"};
    static const char jtypes[][4]={"C1C","L1C","C1L","L1L","C1E","L1E",
        "C2S","L2S","C5Q","L5Q","C6L","L6L","C6S","L6S"};
    static const char itypes[][4]={"C5A","L5A"};
    static const int expected_systems[]={0,0,0,0,0,0,1,1,1,1,2,2,3,3,3,3,3,3,
        4,4,4,4,4,4,5,5,5,5,5,5,5,6};
    static const int expected_signals[]={0,16,17,5,9,14,0,1,5,6,0,6,2,12,17,20,
        6,7,0,7,1,9,11,2,0,16,24,17,14,27,28,0};
    static const double expected_shifts[]={0,.25,-.25,0,0,-.25,0,0,.25,.25,0,0,
        .5,-.25,-.25,-.25,0,-.5,0,.25,0,.25,0,0,0,.25,0,0,-.25,0,0,0};
    int present[32],lli[32],count,index,signal_index;
    double values[32];
    parsed_record_t records[40];
    rnxrange_options_t options;
    rnxrange_report_t report;
    FILE *input=tmpfile(),*output=tmpfile();
    char line[32768],error[512];
    CHECK(input&&output);
    version_header(input,3.05,'M');
    observation_types(input,'G',gtypes,12);
    observation_types(input,'R',rtypes,8);
    observation_types(input,'S',stypes,4);
    observation_types(input,'E',etypes,12);
    observation_types(input,'C',ctypes,12);
    observation_types(input,'J',jtypes,14);
    observation_types(input,'I',itypes,2);
    finish_header(input,0,-4);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0  7\n");
    fill_signal_values(6,present,values,lli);
    satellite_line(input,"G01",12,present,values,lli);
    fill_signal_values(4,present,values,lli);
    satellite_line(input,"R01",8,present,values,lli);
    fill_signal_values(2,present,values,lli);
    satellite_line(input,"S20",4,present,values,lli);
    fill_signal_values(6,present,values,lli);
    satellite_line(input,"E01",12,present,values,lli);
    satellite_line(input,"C06",12,present,values,lli);
    fill_signal_values(7,present,values,lli);
    satellite_line(input,"J04",14,present,values,lli);
    fill_signal_values(1,present,values,lli);
    satellite_line(input,"I01",2,present,values,lli);
    fflush(input);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.source_nonempty_values==64);
    CHECK(report.converted_values==64);
    CHECK(report.emitted_range_records==32);
    CHECK(report.skipped_unsupported_values==0&&report.conversion_errors==0);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(parse_range_line(line,records,40,&count)&&count==32);
    for (index=0;index<count;index++) {
        CHECK(((records[index].status>>16)&7)==expected_systems[index]);
        CHECK(((records[index].status>>21)&0x1F)==expected_signals[index]);
        if (index<6) signal_index=index;
        else if (index<10) signal_index=index-6;
        else if (index<12) signal_index=index-10;
        else if (index<18) signal_index=index-12;
        else if (index<24) signal_index=index-18;
        else if (index<31) signal_index=index-24;
        else signal_index=0;
        CHECK(fabs(records[index].adr-(-(1000.0+signal_index)+
                   expected_shifts[index]))<1E-9);
        CHECK(fabs((-records[index].adr+expected_shifts[index])-
                   (1000.0+signal_index))<1E-9);
    }
    fclose(input);
    fclose(output);
    return 1;
}

static int test_strict_supported_error(void)
{
    static const char types[][4]={"L1C"};
    int present[]={1},lli[]={0};
    double values[]={1000};
    rnxrange_options_t options;
    rnxrange_report_t report;
    FILE *input=tmpfile(),*output=tmpfile();
    char error[512];
    CHECK(input&&output);
    version_header(input,3.05,'G');
    observation_types(input,'G',types,1);
    finish_header(input,0,999);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0  1\n");
    satellite_line(input,"G01",1,present,values,lli);
    fflush(input);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(!run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.source_nonempty_values==1&&report.conversion_errors==1);
    CHECK(strstr(error,"strict conversion failed")!=NULL);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_target_unsupported_and_prn_boundaries(void)
{
    static const char gtypes[][4]={"C1C","C1N","L1N"};
    static const char jtypes[][4]={"C1C"};
    int gp_all[]={1,1,1},gp_code[]={1,0,0},jp[]={1};
    double gv[]={22000000,22000010,1000},jv[]={23000000};
    rnxrange_options_t options;
    rnxrange_report_t report;
    parsed_record_t records[4];
    FILE *input=tmpfile(),*output=tmpfile(),*failed_output,*diagnostic;
    char line[4096],error[512];
    int count,saw_code=0,saw_prn=0,saw_reason_summary=0,saw_code_summary=0;
    CHECK(input&&output);
    version_header(input,3.05,'M');
    observation_types(input,'G',gtypes,3);
    observation_types(input,'J',jtypes,1);
    finish_header(input,0,999);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0  3\n");
    satellite_line(input,"G01",3,gp_all,gv,NULL);
    satellite_line(input,"G33",3,gp_code,gv,NULL);
    satellite_line(input,"J10",1,jp,jv,NULL);
    fflush(input);
    rnxrange_default_options(&options);
    diagnostic=tmpfile();
    CHECK(diagnostic!=NULL);
    rewind(input);
    CHECK(rnxrange_convert(input,output,&options,&report,diagnostic,error,
                           sizeof(error)));
    CHECK(report.source_nonempty_values==5);
    CHECK(report.converted_values==2);
    CHECK(report.skipped_unsupported_values==3);
    CHECK(report.conversion_errors==0);
    CHECK(report.emitted_range_records==2);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(parse_range_line(line,records,4,&count)&&count==2);
    CHECK(records[0].prn==1&&records[1].prn==202);
    rewind(diagnostic);
    while (fgets(line,sizeof(line),diagnostic)) {
        if (strstr(line,"satellite=G01 observation=C1N")) saw_code=1;
        if (strstr(line,"satellite=G33 observation=C1C")&&
            strstr(line,"outside the OEM7 RANGE PRN/slot range")) saw_prn=1;
        if (strstr(line,"skipped_records_by_reason=")) saw_reason_summary=1;
        if (strstr(line,"skipped_values_by_observation_code=")) saw_code_summary=1;
    }
    CHECK(saw_code&&saw_prn&&saw_reason_summary&&saw_code_summary);
    fclose(diagnostic);

    failed_output=tmpfile();
    CHECK(failed_output!=NULL);
    options.emit_warnings=0;
    options.fail_on_unsupported=1;
    CHECK(!run_stream_conversion(input,failed_output,&options,&report,error));
    CHECK(strstr(error,"not supported by OEM7 RANGE")!=NULL);
    fclose(failed_output);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_unknown_snr_unit_is_conversion_error(void)
{
    static const char types[][4]={"C1C","S1C"};
    int present[]={1,1};
    double values[]={22000000,45};
    rnxrange_options_t options;
    rnxrange_report_t report;
    parsed_record_t record;
    FILE *input=tmpfile(),*output=tmpfile(),*best_effort,*assumed;
    char line[4096],error[512];
    int count;
    CHECK(input&&output);
    version_header(input,3.05,'G');
    observation_types(input,'G',types,2);
    finish_header(input,0,999);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0  1\n");
    satellite_line(input,"G01",2,present,values,NULL);
    fflush(input);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(!run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.converted_values==1&&report.conversion_errors==1);
    CHECK(report.skipped_unsupported_values==0&&report.synthetic_cno==1);

    best_effort=tmpfile();
    CHECK(best_effort!=NULL);
    options.strict=0;
    CHECK(run_stream_conversion(input,best_effort,&options,&report,error));
    CHECK(report.converted_values==1&&report.conversion_errors==1);

    assumed=tmpfile();
    CHECK(assumed!=NULL);
    options.strict=1;
    options.assume_snr_dbhz=1;
    CHECK(run_stream_conversion(input,assumed,&options,&report,error));
    CHECK(report.converted_values==2&&report.conversion_errors==0);
    CHECK(report.synthetic_cno==0);
    rewind(assumed);
    CHECK(fgets(line,sizeof(line),assumed)!=NULL);
    CHECK(parse_range_line(line,&record,1,&count)&&count==1);
    CHECK(fabs(record.cno-45.0)<1E-9);
    fclose(assumed);
    fclose(best_effort);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_missing_glonass_fcn_is_strict_error(void)
{
    static const char types[][4]={"C1C","L1C"};
    int present[]={1,1};
    double values[]={22000000,1000};
    rnxrange_options_t options;
    rnxrange_report_t report;
    FILE *input=tmpfile(),*output=tmpfile();
    char error[512];
    CHECK(input&&output);
    version_header(input,3.05,'R');
    observation_types(input,'R',types,2);
    finish_header(input,0,999);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0  1\n");
    satellite_line(input,"R01",2,present,values,NULL);
    fflush(input);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(!run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.source_nonempty_values==2&&report.conversion_errors==2);
    CHECK(report.skipped_unsupported_values==0);
    CHECK(strstr(error,"strict conversion failed")!=NULL);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_event_epoch_resets_locktime(void)
{
    static const char types[][4]={"C1C","L1C"};
    int present[]={1,1};
    double values[]={22000000,1000};
    const char *epochs[]={
        "> 2024 01 01 00 00  0.0000000  0  1\n",
        "> 2024 01 01 00 00 30.0000000  0  1\n"
    };
    rnxrange_options_t options;
    rnxrange_report_t report;
    FILE *input=tmpfile(),*output=tmpfile();
    char line[4096],error[512];
    int count;
    parsed_record_t record;
    CHECK(input&&output);
    version_header(input,3.05,'G');
    observation_types(input,'G',types,2);
    finish_header(input,0,999);
    fputs(epochs[0],input);
    satellite_line(input,"G01",2,present,values,NULL);
    fprintf(input,"> 2024 01 01 00 00 15.0000000  4  2\n");
    header_line(input,"event one","COMMENT");
    header_line(input,"event two","COMMENT");
    fputs(epochs[1],input);
    satellite_line(input,"G01",2,present,values,NULL);
    fflush(input);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.input_epochs==2&&report.output_messages==2);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(parse_range_line(line,&record,1,&count)&&count==1);
    CHECK(fabs(record.locktime)<1E-9);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_incomplete_epoch_is_not_reported_or_emitted(void)
{
    static const char types[][4]={"C1C"};
    int present[]={1};
    double values[]={22000000};
    rnxrange_options_t options;
    rnxrange_report_t report;
    FILE *input=tmpfile(),*output=tmpfile();
    char line[4096],error[512];
    CHECK(input&&output);
    version_header(input,3.05,'G');
    observation_types(input,'G',types,1);
    finish_header(input,0,999);
    fprintf(input,"> 2024 01 01 00 00  0.0000000  0  1\n");
    satellite_line(input,"G01",1,present,values,NULL);
    fprintf(input,"> 2024 01 01 00 00 30.0000000  0  2\n");
    satellite_line(input,"G01",1,present,values,NULL);
    fflush(input);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(!run_stream_conversion(input,output,&options,&report,error));
    CHECK(strstr(error,"expected 2 satellite records, found 1")!=NULL);
    CHECK(report.input_epochs==1&&report.output_messages==1);
    CHECK(report.source_satellites==1&&report.source_nonempty_values==1);
    CHECK(report.emitted_range_records==1&&report.converted_values==1);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(fgets(line,sizeof(line),output)==NULL);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_time_systems_and_week_rollover(void)
{
    static const char types[][4]={"C1C"};
    static const struct {
        const char *time_system;
        const char *epoch;
        const char *expected_header_time;
    } cases[]={
        {"BDT","> 2024 01 01 00 00  0.0000000  0  1\n",",2295,86414.000,"},
        {"UTC","> 2024 01 01 00 00  0.0000000  0  1\n",",2295,86418.000,"},
        {"GPS","> 2024 01 06 23 59 59.9996000  0  1\n",",2296,0.000,"}
    };
    int present[]={1},lli[]={0};
    double values[]={22000000};
    size_t i;
    for (i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        rnxrange_options_t options;
        rnxrange_report_t report;
        parsed_record_t record;
        FILE *input=tmpfile(),*output=tmpfile();
        char line[4096],error[512];
        int count;
        CHECK(input&&output);
        version_header(input,3.05,'G');
        observation_types(input,'G',types,1);
        header_line(input,"     1","RCV CLOCK OFFS APPL");
        finish_header_time(input,0,999,cases[i].time_system);
        fputs(cases[i].epoch,input);
        satellite_line(input,"G01",1,present,values,lli);
        fflush(input);
        rnxrange_default_options(&options);
        options.emit_warnings=0;
        CHECK(run_stream_conversion(input,output,&options,&report,error));
        rewind(output);
        CHECK(fgets(line,sizeof(line),output)!=NULL);
        CHECK(strstr(line,cases[i].expected_header_time)!=NULL);
        CHECK(parse_range_line(line,&record,1,&count)&&count==1);
        fclose(input);
        fclose(output);
    }
    return 1;
}

static int test_locktime_and_lli_cycle_slip(void)
{
    static const char types[][4]={"C1C","L1C"};
    int present[]={1,1},lli[]={0,0};
    double values[]={22000000,1000};
    const double expected_locktimes[]={0.0,30.0,0.0,30.0};
    rnxrange_options_t options;
    rnxrange_report_t report;
    FILE *input=tmpfile(),*output=tmpfile();
    char line[4096],error[512];
    int epoch,count;
    CHECK(input&&output);
    version_header(input,3.05,'G');
    observation_types(input,'G',types,2);
    finish_header(input,0,999);
    for (epoch=0;epoch<4;epoch++) {
        fprintf(input,"> 2024 01 01 00 %02d %2d.0000000  0  1\n",
                epoch/2,(epoch&1)?30:0);
        lli[1]=epoch==2?1:(epoch==3?2:0);
        satellite_line(input,"G01",2,present,values,lli);
    }
    fflush(input);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.input_epochs==4&&report.emitted_range_records==4);
    rewind(output);
    for (epoch=0;epoch<4;epoch++) {
        parsed_record_t record;
        CHECK(fgets(line,sizeof(line),output)!=NULL);
        CHECK(parse_range_line(line,&record,1,&count)&&count==1);
        CHECK(fabs(record.locktime-expected_locktimes[epoch])<1E-9);
        CHECK(((record.status>>11)&1U)==(epoch==3?0U:1U));
        CHECK(((record.status>>28)&1U)==0U);
    }
    CHECK(fgets(line,sizeof(line),output)==NULL);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_repository_real_fixture(void)
{
    rnxrange_options_t options;
    rnxrange_report_t report;
    parsed_record_t records[32];
    FILE *input=fopen("../../test/data/rinex/07590920.05o","rb");
    FILE *output=tmpfile();
    char line[32768],error[512];
    int count;
    CHECK(input&&output);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.input_epochs==120);
    CHECK(report.source_satellites==948);
    CHECK(report.emitted_range_records==1872);
    CHECK(report.source_nonempty_values==3740);
    CHECK(report.converted_values==3740);
    CHECK(report.skipped_unsupported_values==0);
    CHECK(report.conversion_errors==0);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(strstr(line,"*e2caacdd\r\n")!=NULL); /* fixed golden vector */
    CHECK(parse_range_line(line,records,32,&count)&&count==16);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_static_rinex4_fixture(void)
{
    rnxrange_options_t options;
    rnxrange_report_t report;
    parsed_record_t record;
    FILE *input=fopen("data/rinex4_mixed_obs.rnx","rb");
    FILE *output=tmpfile();
    char line[4096],error[512];
    int count;
    CHECK(input&&output);
    rnxrange_default_options(&options);
    options.emit_warnings=0;
    CHECK(run_stream_conversion(input,output,&options,&report,error));
    CHECK(report.source_nonempty_values==5&&report.converted_values==5);
    CHECK(report.emitted_range_records==1&&report.conversion_errors==0);
    rewind(output);
    CHECK(fgets(line,sizeof(line),output)!=NULL);
    CHECK(parse_range_line(line,&record,1,&count)&&count==1);
    CHECK(record.prn==1&&fabs(record.adr+1000.0)<1E-9);
    CHECK(fabs(record.doppler+100.0)<1E-9&&fabs(record.cno-45.0)<1E-9);
    CHECK(((record.status>>5)&0x1F)==4);
    CHECK(((record.status>>11)&1U)==0U&&((record.status>>28)&1U)==0U);
    fclose(input);
    fclose(output);
    return 1;
}

static int test_legacy_maxobs_boundary(void)
{
    obs_t observations={0};
    nav_t navigation={0};
    sta_t station={0};
    CHECK(readrnx("data/legacy_70_satellites.rnx",1,"",&observations,
                  &navigation,&station));
    CHECK(observations.n==MAXOBS);
    free(observations.data);
    freenav(&navigation,0x3FF);
    return 1;
}

int main(void)
{
    int passed=0,total=0;
    total++; if (test_mixed_mapping_and_crc()) passed++;
    total++; if (test_rinex2_mapping()) passed++;
    total++; if (test_dynamic_capacity()) passed++;
    total++; if (test_all_official_signal_mappings()) passed++;
    total++; if (test_strict_supported_error()) passed++;
    total++; if (test_target_unsupported_and_prn_boundaries()) passed++;
    total++; if (test_unknown_snr_unit_is_conversion_error()) passed++;
    total++; if (test_missing_glonass_fcn_is_strict_error()) passed++;
    total++; if (test_time_systems_and_week_rollover()) passed++;
    total++; if (test_locktime_and_lli_cycle_slip()) passed++;
    total++; if (test_event_epoch_resets_locktime()) passed++;
    total++; if (test_incomplete_epoch_is_not_reported_or_emitted()) passed++;
    total++; if (test_static_rinex4_fixture()) passed++;
    total++; if (test_legacy_maxobs_boundary()) passed++;
    total++; if (test_repository_real_fixture()) passed++;
    printf("rnx2range tests: %d/%d passed\n",passed,total);
    return passed==total?0:1;
}
