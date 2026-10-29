# Expands a terse spec into Valis Turtle.
import re
def fmt(v):
    if isinstance(v, str): return '"%s"' % v
    return repr(float(v)) if isinstance(v, float) else str(v)
def build(name, label, header, genre, role, accepts, nodes, arcs, params, path):
    """nodes: [(id, cls, {prop: val}, comment)], arcs: [(a,port,b,port)],
    params: [(slot,id,prop,name,sym,min,max,section,unit)]"""
    o = ["# examples/%s.ttl" % name, "#"] + ["# " + l if l else "#" for l in header.split("\n")]
    o += ["", "@prefix val:   <http://purl.org/stuff/valis/> .",
          "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .",
          "@prefix units: <http://lv2plug.in/ns/extensions/units#> .",
          "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .",
          "@prefix trn:   <http://purl.org/stuff/transmissions/> .",
          "@prefix :      <urn:valis:%s#> ." % name.replace('-', ''), ""]
    ids = [n[0] for n in nodes]
    arcn = []
    for i, (a, ap, b, bp) in enumerate(arcs):
        arcn.append("a_%s_%s__%s_%s" % (a, ap, b, bp))
    o.append(":%s a val:Circuit , trn:PluginProfile ;" % name.replace('-', ''))
    o.append("    trn:role trn:%s ;" % role)
    o.append("    trn:accepts trn:%s ;" % accepts)
    o.append("    trn:produces trn:Audio ;")
    o.append("    trn:genre %s ;" % ", ".join('"%s"' % g for g in genre))
    o.append('    rdfs:label "%s" ;' % label)
    o.append("    val:element " + " , ".join(":" + i for i in ids) + " ;")
    o.append("    val:arc " + " ,\n        ".join(":" + a for a in arcn) + " .\n")
    for n in nodes:
        i, c, p, *cm = n
        if cm and cm[0]: o.append("# " + cm[0])
        props = "".join(" ;\n    val:%s %s" % (k, fmt(v)) for k, v in p.items())
        o.append(":%s a val:%s%s .\n" % (i, c, props))
    for an, (a, ap, b, bp) in zip(arcn, arcs):
        o.append(":%s a val:Arc ;\n    val:from [ val:node :%s ; val:port \"%s\" ] ;\n    val:to   [ val:node :%s ; val:port \"%s\" ] ." % (an, a, ap, b, bp))
    o.append("")
    for (s, t, pr, nm, sym, lo, hi, sec, unit) in params:
        u = (" ;\n    units:unit units:%s" % unit) if unit else ""
        o.append(":p%d a val:Param ; val:slot %d ;\n    val:target :%s ; val:property val:%s ;\n    lv2:name \"%s\" ; lv2:symbol \"%s\" ;\n    lv2:minimum %s ; lv2:maximum %s ;\n    val:section \"%s\"%s ." % (s, s, t, pr, nm, sym, lo, hi, sec, u))
    open(path, "w").write("\n".join(o) + "\n")

def build_poly(name, label, header, genre, vnodes, varcs, vports, vout, voices, nodes, arcs, params, path):
    """A polyphonic instrument: a Voice subcircuit stamped out `voices` times as :poly,
    followed by the shared nodes/arcs. vports: [(sym, label, default, min, max, node, port, unit, log)].
    params target :poly by port symbol: (slot, sym, name, symbol, section, unit)."""
    cid = name.replace('-', '')
    o = ["# examples/%s.ttl" % name, "#"] + ["# " + l if l else "#" for l in header.split("\n")]
    o += ["", "@prefix val:   <http://purl.org/stuff/valis/> .",
          "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .",
          "@prefix units: <http://lv2plug.in/ns/extensions/units#> .",
          "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .",
          "@prefix trn:   <http://purl.org/stuff/transmissions/> .",
          "@prefix :      <urn:valis:%s#> ." % cid, "",
          ":Voice a val:Subcircuit ;", '    rdfs:label "Voice" ;', ""]
    ports = ['    lv2:port [ a lv2:OutputPort , lv2:AudioPort ;\n               lv2:symbol "out" ; lv2:name "Out" ;\n               val:node :%s ; val:port "%s" ]' % vout]
    for (sym, lab, d, lo, hi, node, port, unit, log) in vports:
        extra = (" ;\n               lv2:portProperty lv2:logarithmic" if log else "") + ((" ;\n               units:unit units:%s" % unit) if unit else "")
        ports.append('             [ a lv2:InputPort , lv2:ControlPort ;\n               lv2:symbol "%s" ; lv2:name "%s" ;\n               lv2:default %s ; lv2:minimum %s ; lv2:maximum %s%s ;\n               val:node :%s ; val:port "%s" ]' % (sym, lab, d, lo, hi, extra, node, port))
    o.append(" ,\n\n".join(ports) + " ;\n")
    varcn = ["v_%s_%s__%s_%s" % a for a in varcs]
    o.append("    val:element " + " , ".join(":" + n[0] for n in vnodes) + " ;")
    o.append("    val:arc " + " ,\n        ".join(":" + a for a in varcn) + " .\n")
    for n in vnodes:
        i, c, p = n[0], n[1], n[2]
        props = "".join(" ;\n    val:%s %s" % (k, fmt(v)) for k, v in p.items())
        o.append(":%s a val:%s%s .\n" % (i, c, props))
    for an, (a, ap, b, bp) in zip(varcn, varcs):
        o.append(":%s a val:Arc ;\n    val:from [ val:node :%s ; val:port \"%s\" ] ;\n    val:to   [ val:node :%s ; val:port \"%s\" ] ." % (an, a, ap, b, bp))
    o.append("")
    ids = ["poly"] + [n[0] for n in nodes]
    arcn = ["a_%s_%s__%s_%s" % a for a in arcs]
    o += [":%s a val:Circuit , trn:PluginProfile ;" % cid, "    trn:role trn:AudioInstrument ;", "    trn:accepts trn:Midi ;",
          "    trn:produces trn:Audio ;", "    trn:genre %s ;" % ", ".join('"%s"' % g for g in genre),
          '    rdfs:label "%s" ;' % label, "    val:element " + " , ".join(":" + i for i in ids) + " ;",
          "    val:arc " + " ,\n        ".join(":" + a for a in arcn) + " .\n",
          ":poly a :Voice ; val:voices %d .\n" % voices]
    for n in nodes:
        i, c, p = n[0], n[1], n[2]
        props = "".join(" ;\n    val:%s %s" % (k, fmt(v)) for k, v in p.items())
        o.append(":%s a val:%s%s .\n" % (i, c, props))
    for an, (a, ap, b, bp) in zip(arcn, arcs):
        o.append(":%s a val:Arc ;\n    val:from [ val:node :%s ; val:port \"%s\" ] ;\n    val:to   [ val:node :%s ; val:port \"%s\" ] ." % (an, a, ap, b, bp))
    o.append("")
    for (s, sym, nm, psym, sec, unit) in params:
        lo, hi = [(v[3], v[4]) for v in vports if v[0] == sym][0] if any(v[0] == sym for v in vports) else (0, 1)
        u = (" ;\n    units:unit units:%s" % unit) if unit else ""
        o.append(":p%d a val:Param ; val:slot %d ;\n    val:target :poly ; val:property val:%s ;\n    lv2:name \"%s\" ; lv2:symbol \"%s\" ;\n    lv2:minimum %s ; lv2:maximum %s ;\n    val:section \"%s\"%s ." % (s, s, sym, nm, psym, lo, hi, sec, u))
    open(path, "w").write("\n".join(o) + "\n")
