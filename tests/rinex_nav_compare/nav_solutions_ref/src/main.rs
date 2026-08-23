use rinex::{
    navigation::{IonosphereModel, NavFrame},
    prelude::Rinex,
};
use serde_json::{json, Value};
use std::{env, fs::File, io::{BufWriter, Write}};

fn emit<W: Write>(out: &mut W, base: &Value, field: &str, value: Value) {
    let mut row = base.clone();
    let object = row.as_object_mut().expect("base object");
    object.insert("field".to_string(), json!(field));
    object.insert("value".to_string(), value);
    writeln!(out, "{}", serde_json::to_string(&row).expect("serialize row"))
        .expect("write row");
}

fn base(key: &rinex::navigation::NavKey) -> Value {
    json!({
        "record_type": key.frmtype.to_string(),
        "system": key.sv.constellation.to_string(),
        "sv": key.sv.to_string(),
        "prn": key.sv.prn,
        "message_type": key.msgtype.to_string(),
        "epoch": key.epoch.to_string(),
    })
}

fn main() {
    let path = env::args().nth(1).expect("usage: nav_solutions_ref_dump FILE [OUT]");
    let output = env::args().nth(2).unwrap_or_else(|| "-".to_string());
    let rnx = Rinex::from_file(&path).expect("parse NAV file");
    assert!(rnx.is_navigation_rinex(), "not a NAV file");
    let mut out: Box<dyn Write> = if output == "-" {
        Box::new(std::io::sink())
    } else {
        Box::new(BufWriter::new(File::create(output).expect("create output")))
    };

    let record = rnx.record.as_nav().expect("NAV record");
    for (key, frame) in record {
        let b = base(key);
        match frame {
            NavFrame::EPH(eph) => {
                emit(&mut out, &b, "clock_bias", json!(eph.clock_bias));
                emit(&mut out, &b, "clock_drift", json!(eph.clock_drift));
                emit(&mut out, &b, "clock_drift_rate", json!(eph.clock_drift_rate));
                let mut fields: Vec<_> = eph.orbits.iter().collect();
                fields.sort_by_key(|(name, _)| *name);
                for (name, value) in fields {
                    emit(&mut out, &b, name, serde_json::to_value(value).expect("orbit value"));
                }
            }
            NavFrame::ION(model) => match model {
                IonosphereModel::Klobuchar(kb) => {
                    for (i, value) in [kb.alpha.0, kb.alpha.1, kb.alpha.2, kb.alpha.3]
                        .into_iter().enumerate()
                    {
                        emit(&mut out, &b, &format!("alpha[{i}]"), json!(value));
                    }
                    for (i, value) in [kb.beta.0, kb.beta.1, kb.beta.2, kb.beta.3]
                        .into_iter().enumerate()
                    {
                        emit(&mut out, &b, &format!("beta[{i}]"), json!(value));
                    }
                    emit(&mut out, &b, "region", json!(format!("{:?}", kb.region)));
                }
                IonosphereModel::NequickG(ng) => {
                    for (i, value) in [ng.a.0, ng.a.1, ng.a.2].into_iter().enumerate() {
                        emit(&mut out, &b, &format!("a[{i}]"), json!(value));
                    }
                    emit(&mut out, &b, "region", json!(ng.region.bits()));
                }
                IonosphereModel::Bdgim(bd) => {
                    for (i, value) in [
                        bd.alpha.0, bd.alpha.1, bd.alpha.2, bd.alpha.3, bd.alpha.4,
                        bd.alpha.5, bd.alpha.6, bd.alpha.7, bd.alpha.8,
                    ].into_iter().enumerate()
                    {
                        emit(&mut out, &b, &format!("alpha[{i}]"), json!(value));
                    }
                }
            },
            NavFrame::EOP(eop) => {
                for (name, value) in [
                    ("x[0]", eop.x.0), ("x[1]", eop.x.1), ("x[2]", eop.x.2),
                    ("y[0]", eop.y.0), ("y[1]", eop.y.1), ("y[2]", eop.y.2),
                    ("t_tm", eop.t_tm as f64),
                    ("delta_ut1[0]", eop.delta_ut1.0),
                    ("delta_ut1[1]", eop.delta_ut1.1),
                    ("delta_ut1[2]", eop.delta_ut1.2),
                ] {
                    emit(&mut out, &b, name, json!(value));
                }
            }
            NavFrame::STO(sto) => {
                emit(&mut out, &b, "lhs", json!(sto.lhs.to_string()));
                emit(&mut out, &b, "rhs", json!(sto.rhs.to_string()));
                emit(&mut out, &b, "t_ref.week", json!(sto.t_ref.0));
                emit(&mut out, &b, "t_ref.nanos", json!(sto.t_ref.1));
                emit(&mut out, &b, "utc", json!(sto.utc));
                emit(&mut out, &b, "polynomial[0]", json!(sto.polynomial.0));
                emit(&mut out, &b, "polynomial[1]", json!(sto.polynomial.1));
                emit(&mut out, &b, "polynomial[2]", json!(sto.polynomial.2));
            }
        }
    }
}
