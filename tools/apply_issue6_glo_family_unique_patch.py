#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(rel, old, new):
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{rel}: expected one guarded match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/rtkcmn.c",
    """static int cmpgeph(const void *p1, const void *p2)
{
    geph_t *q1=(geph_t *)p1,*q2=(geph_t *)p2;
    return q1->tof.time!=q2->tof.time?(int)(q1->tof.time-q2->tof.time):
           (q1->toe.time!=q2->toe.time?(int)(q1->toe.time-q2->toe.time):
            q1->sat-q2->sat);
}
""",
    """static int cmpgeph(const void *p1, const void *p2)
{
    geph_t *q1=(geph_t *)p1,*q2=(geph_t *)p2;
    if (q1->tof.time!=q2->tof.time) return (int)(q1->tof.time-q2->tof.time);
    if (q1->toe.time!=q2->toe.time) return (int)(q1->toe.time-q2->toe.time);
    if (q1->sat!=q2->sat) return q1->sat-q2->sat;
    return q1->hdr.msg_type-q2->hdr.msg_type;
}
""",
)

replace_once(
    "src/rtkcmn.c",
    """        if (nav->geph[i].sat!=nav->geph[j].sat||
            nav->geph[i].toe.time!=nav->geph[j].toe.time||
            nav->geph[i].svh!=nav->geph[j].svh) {
""",
    """        if (nav->geph[i].sat!=nav->geph[j].sat||
            nav->geph[i].toe.time!=nav->geph[j].toe.time||
            nav->geph[i].svh!=nav->geph[j].svh||
            nav->geph[i].hdr.msg_type!=nav->geph[j].hdr.msg_type) {
""",
)

replace_once(
    "src/rinex.c",
    """    /* There is no legacy 7-bit tb/IODE field in the CDMA RINEX record. Keep a
       deterministic epoch tag for diagnostics/selector provenance. */
    geph->iode=(int)(fmod(tow+10800.0,86400.0)/90.0+0.5);
""",
    """    /* RINEX4 GLONASS CDMA has no legacy FDMA IODE/tb field. Mark it
       unavailable instead of manufacturing an FDMA-looking issue number. */
    geph->iode=-1;
""",
)

replace_once(
    "src/rinex.c",
    """    geph->flag=(int)data[16]; /* RINEX4 RT/RE source flags */
    geph->age=(int)data[17];  /* AODE, retained for diagnostics */
    geph->sva=(int)data[31];  /* orbit accuracy index */
""",
    """    /* data[15..18] are CDMA satellite type/source/AODE/AODC fields and
       must not be overloaded into legacy FDMA flag/age semantics. */
    geph->sva=(int)data[31];  /* orbit accuracy index */
""",
)

# Strengthen parser regression: same satellite and same Toc for L3OC and FDMA,
# then call uniqnav(). Both message families must survive deduplication.
replace_once(
    "tests/rinex4_glo_l3oc/test_rinex4_glo_l3oc.c",
    """    geph_t *g;
    int stat,ok=1;
""",
    """    geph_t *g,*l3=NULL,*fdma=NULL;
    int stat,ok=1,i;
""",
)
replace_once(
    "tests/rinex4_glo_l3oc/test_rinex4_glo_l3oc.c",
    """    fprintf(fp,"> EPH R27 FDMA\\n");
    fprintf(fp,"R27 2026 08 27 10 30 00%19.12E%19.12E%19.12E\\n",-1E-4,2E-10,383400.0);
""",
    """    fprintf(fp,"> EPH R26 FDMA\\n");
    fprintf(fp,"R26 2026 08 27 10 15 00%19.12E%19.12E%19.12E\\n",-1E-4,2E-10,382500.0);
""",
)
replace_once(
    "tests/rinex4_glo_l3oc/test_rinex4_glo_l3oc.c",
    """    stat=readrnx(path,1,"",NULL,&nav,NULL);
    remove(path);
    if (!stat||nav.ng!=2) {
        fprintf(stderr,"parser alignment failed: stat=%d ng=%d\\n",stat,nav.ng);
        free(nav.geph);
        return 1;
    }

    g=&nav.geph[0];
    if (g->hdr.msg_type!=NAV_L3OC||fabs(g->beta-3E-13)>1E-18||
        fabs(g->isc_l3ocp-2.5E-8)>1E-15||g->data_validity!=0||
        fabs(g->pc[0]-0.5)>1E-12||fabs(g->pc[1]+0.25)>1E-12||
        fabs(g->pc[2]-1.0)>1E-12||fabs(g->ttm-382500.0)>1E-9) {
        fprintf(stderr,"L3OC fields decoded incorrectly\\n");
        ok=0;
    }
    g=&nav.geph[1];
    if (g->hdr.msg_type!=NAV_FDMA||g->frq!=-4) {
        fprintf(stderr,"record following L3OC was not aligned/decoded\\n");
        ok=0;
    }
""",
    """    stat=readrnx(path,1,"",NULL,&nav,NULL);
    remove(path);
    if (!stat) {
        fprintf(stderr,"RINEX4 GLONASS fixture was not read\\n");
        free(nav.geph);
        return 1;
    }
    uniqnav(&nav);
    if (nav.ng!=2) {
        fprintf(stderr,"GLONASS family dedup failed: ng=%d\\n",nav.ng);
        free(nav.geph);
        return 1;
    }
    for (i=0;i<nav.ng;i++) {
        if (nav.geph[i].hdr.msg_type==NAV_L3OC) l3=&nav.geph[i];
        if (nav.geph[i].hdr.msg_type==NAV_FDMA) fdma=&nav.geph[i];
    }
    if (!l3||!fdma) {
        fprintf(stderr,"same-epoch L3OC/FDMA families were not both retained\\n");
        free(nav.geph);
        return 1;
    }

    g=l3;
    if (g->iode!=-1||fabs(g->beta-3E-13)>1E-18||
        fabs(g->isc_l3ocp-2.5E-8)>1E-15||g->data_validity!=0||
        fabs(g->pc[0]-0.5)>1E-12||fabs(g->pc[1]+0.25)>1E-12||
        fabs(g->pc[2]-1.0)>1E-12||fabs(g->ttm-382500.0)>1E-9) {
        fprintf(stderr,"L3OC fields decoded incorrectly\\n");
        ok=0;
    }
    g=fdma;
    if (g->frq!=-4) {
        fprintf(stderr,"same-epoch FDMA record was not aligned/decoded\\n");
        ok=0;
    }
""",
)

print("GLONASS family-retention patch applied successfully")
