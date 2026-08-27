from pathlib import Path

path = Path("src/rinex.c")
text = path.read_text()
old = '''    //decode BDS CNAV
    if(hdr->sys == SYS_CMP && (hdr->msg_type &(NAV_CNV1 | NAV_CNV2 | NAV_CNV3))) {
        eph->Adot = data[3];'''
new = '''    //decode BDS CNAV
    if(hdr->sys == SYS_CMP && (hdr->msg_type &(NAV_CNV1 | NAV_CNV2 | NAV_CNV3))) {
        /* RINEX4 BDS CNV1/2/3 does not carry a BDT week field. Derive the
         * continuous BDT week from Toc and combine it with the Orbit-3 Toe. */
        eph->week = week;
        eph->toes = data[11];
        eph->toe = bdt2gpst(bdt2time(eph->week,eph->toes));
        eph->toe = adjweek(eph->toe,eph->toc);
        eph->Adot = data[3];'''
if text.count(old) != 1:
    raise RuntimeError(f"BDS CNAV decoder anchor: expected 1, found {text.count(old)}")
text = text.replace(old, new, 1)
old = '''            eph->ttr=bdt2gpst(bdt2time(week,data[35])); /* bdt -> gpst */
            eph->ttr=adjweek(eph->ttr,toc);'''
new = '''            eph->ttr=bdt2gpst(bdt2time(eph->week,data[35])); /* bdt -> gpst */
            eph->ttr=adjweek(eph->ttr,eph->toc);'''
if text.count(old) != 1:
    raise RuntimeError(f"BDS CNV1/2 ttr anchor: expected 1, found {text.count(old)}")
text = text.replace(old, new, 1)
old = '''            eph->ttr=bdt2gpst(bdt2time(week,data[31])); /* bdt -> gpst */
            eph->ttr=adjweek(eph->ttr,toc);'''
new = '''            eph->ttr=bdt2gpst(bdt2time(eph->week,data[31])); /* bdt -> gpst */
            eph->ttr=adjweek(eph->ttr,eph->toc);'''
if text.count(old) != 1:
    raise RuntimeError(f"BDS CNV3 ttr anchor: expected 1, found {text.count(old)}")
text = text.replace(old, new, 1)
path.write_text(text)
print("BDS RINEX4 CNAV week/Toe fix applied")
