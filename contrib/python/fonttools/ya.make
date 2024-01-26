# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(4.47.2)

LICENSE(MIT)

NO_LINT()

NO_CHECK_IMPORTS(
    fontTools.misc.symfont
    fontTools.pens.*
    fontTools.ttLib.removeOverlaps
    fontTools.ufoLib.*
    fontTools.varLib.interpolatablePlot
    fontTools.varLib.plot
)

PY_SRCS(
    TOP_LEVEL
    fontTools/__init__.py
    fontTools/__main__.py
    fontTools/afmLib.py
    fontTools/agl.py
    fontTools/cffLib/__init__.py
    fontTools/cffLib/specializer.py
    fontTools/cffLib/width.py
    fontTools/colorLib/__init__.py
    fontTools/colorLib/builder.py
    fontTools/colorLib/errors.py
    fontTools/colorLib/geometry.py
    fontTools/colorLib/table_builder.py
    fontTools/colorLib/unbuilder.py
    fontTools/config/__init__.py
    fontTools/cu2qu/__init__.py
    fontTools/cu2qu/__main__.py
    fontTools/cu2qu/benchmark.py
    fontTools/cu2qu/cli.py
    fontTools/cu2qu/cu2qu.py
    fontTools/cu2qu/errors.py
    fontTools/cu2qu/ufo.py
    fontTools/designspaceLib/__init__.py
    fontTools/designspaceLib/split.py
    fontTools/designspaceLib/statNames.py
    fontTools/designspaceLib/types.py
    fontTools/encodings/MacRoman.py
    fontTools/encodings/StandardEncoding.py
    fontTools/encodings/__init__.py
    fontTools/encodings/codecs.py
    fontTools/feaLib/__init__.py
    fontTools/feaLib/__main__.py
    fontTools/feaLib/ast.py
    fontTools/feaLib/builder.py
    fontTools/feaLib/error.py
    fontTools/feaLib/lexer.py
    fontTools/feaLib/location.py
    fontTools/feaLib/lookupDebugInfo.py
    fontTools/feaLib/parser.py
    fontTools/feaLib/variableScalar.py
    fontTools/fontBuilder.py
    fontTools/help.py
    fontTools/merge/__init__.py
    fontTools/merge/__main__.py
    fontTools/merge/base.py
    fontTools/merge/cmap.py
    fontTools/merge/layout.py
    fontTools/merge/options.py
    fontTools/merge/tables.py
    fontTools/merge/unicode.py
    fontTools/merge/util.py
    fontTools/misc/__init__.py
    fontTools/misc/arrayTools.py
    fontTools/misc/bezierTools.py
    fontTools/misc/classifyTools.py
    fontTools/misc/cliTools.py
    fontTools/misc/configTools.py
    fontTools/misc/cython.py
    fontTools/misc/dictTools.py
    fontTools/misc/eexec.py
    fontTools/misc/encodingTools.py
    fontTools/misc/etree.py
    fontTools/misc/filenames.py
    fontTools/misc/fixedTools.py
    fontTools/misc/intTools.py
    fontTools/misc/loggingTools.py
    fontTools/misc/macCreatorType.py
    fontTools/misc/macRes.py
    fontTools/misc/plistlib/__init__.py
    fontTools/misc/psCharStrings.py
    fontTools/misc/psLib.py
    fontTools/misc/psOperators.py
    fontTools/misc/py23.py
    fontTools/misc/roundTools.py
    fontTools/misc/sstruct.py
    fontTools/misc/symfont.py
    fontTools/misc/testTools.py
    fontTools/misc/textTools.py
    fontTools/misc/timeTools.py
    fontTools/misc/transform.py
    fontTools/misc/treeTools.py
    fontTools/misc/vector.py
    fontTools/misc/visitor.py
    fontTools/misc/xmlReader.py
    fontTools/misc/xmlWriter.py
    fontTools/mtiLib/__init__.py
    fontTools/mtiLib/__main__.py
    fontTools/otlLib/__init__.py
    fontTools/otlLib/builder.py
    fontTools/otlLib/error.py
    fontTools/otlLib/maxContextCalc.py
    fontTools/otlLib/optimize/__init__.py
    fontTools/otlLib/optimize/__main__.py
    fontTools/otlLib/optimize/gpos.py
    fontTools/pens/__init__.py
    fontTools/pens/areaPen.py
    fontTools/pens/basePen.py
    fontTools/pens/boundsPen.py
    fontTools/pens/cairoPen.py
    fontTools/pens/cocoaPen.py
    fontTools/pens/cu2quPen.py
    fontTools/pens/explicitClosingLinePen.py
    fontTools/pens/filterPen.py
    fontTools/pens/freetypePen.py
    fontTools/pens/hashPointPen.py
    fontTools/pens/momentsPen.py
    fontTools/pens/perimeterPen.py
    fontTools/pens/pointInsidePen.py
    fontTools/pens/pointPen.py
    fontTools/pens/qtPen.py
    fontTools/pens/qu2cuPen.py
    fontTools/pens/quartzPen.py
    fontTools/pens/recordingPen.py
    fontTools/pens/reportLabPen.py
    fontTools/pens/reverseContourPen.py
    fontTools/pens/roundingPen.py
    fontTools/pens/statisticsPen.py
    fontTools/pens/svgPathPen.py
    fontTools/pens/t2CharStringPen.py
    fontTools/pens/teePen.py
    fontTools/pens/transformPen.py
    fontTools/pens/ttGlyphPen.py
    fontTools/pens/wxPen.py
    fontTools/qu2cu/__init__.py
    fontTools/qu2cu/__main__.py
    fontTools/qu2cu/benchmark.py
    fontTools/qu2cu/cli.py
    fontTools/qu2cu/qu2cu.py
    fontTools/subset/__init__.py
    fontTools/subset/__main__.py
    fontTools/subset/cff.py
    fontTools/subset/svg.py
    fontTools/subset/util.py
    fontTools/svgLib/__init__.py
    fontTools/svgLib/path/__init__.py
    fontTools/svgLib/path/arc.py
    fontTools/svgLib/path/parser.py
    fontTools/svgLib/path/shapes.py
    fontTools/t1Lib/__init__.py
    fontTools/tfmLib.py
    fontTools/ttLib/__init__.py
    fontTools/ttLib/__main__.py
    fontTools/ttLib/macUtils.py
    fontTools/ttLib/removeOverlaps.py
    fontTools/ttLib/scaleUpem.py
    fontTools/ttLib/sfnt.py
    fontTools/ttLib/standardGlyphOrder.py
    fontTools/ttLib/tables/B_A_S_E_.py
    fontTools/ttLib/tables/BitmapGlyphMetrics.py
    fontTools/ttLib/tables/C_B_D_T_.py
    fontTools/ttLib/tables/C_B_L_C_.py
    fontTools/ttLib/tables/C_F_F_.py
    fontTools/ttLib/tables/C_F_F__2.py
    fontTools/ttLib/tables/C_O_L_R_.py
    fontTools/ttLib/tables/C_P_A_L_.py
    fontTools/ttLib/tables/D_S_I_G_.py
    fontTools/ttLib/tables/D__e_b_g.py
    fontTools/ttLib/tables/DefaultTable.py
    fontTools/ttLib/tables/E_B_D_T_.py
    fontTools/ttLib/tables/E_B_L_C_.py
    fontTools/ttLib/tables/F_F_T_M_.py
    fontTools/ttLib/tables/F__e_a_t.py
    fontTools/ttLib/tables/G_D_E_F_.py
    fontTools/ttLib/tables/G_M_A_P_.py
    fontTools/ttLib/tables/G_P_K_G_.py
    fontTools/ttLib/tables/G_P_O_S_.py
    fontTools/ttLib/tables/G_S_U_B_.py
    fontTools/ttLib/tables/G__l_a_t.py
    fontTools/ttLib/tables/G__l_o_c.py
    fontTools/ttLib/tables/H_V_A_R_.py
    fontTools/ttLib/tables/J_S_T_F_.py
    fontTools/ttLib/tables/L_T_S_H_.py
    fontTools/ttLib/tables/M_A_T_H_.py
    fontTools/ttLib/tables/M_E_T_A_.py
    fontTools/ttLib/tables/M_V_A_R_.py
    fontTools/ttLib/tables/O_S_2f_2.py
    fontTools/ttLib/tables/S_I_N_G_.py
    fontTools/ttLib/tables/S_T_A_T_.py
    fontTools/ttLib/tables/S_V_G_.py
    fontTools/ttLib/tables/S__i_l_f.py
    fontTools/ttLib/tables/S__i_l_l.py
    fontTools/ttLib/tables/T_S_I_B_.py
    fontTools/ttLib/tables/T_S_I_C_.py
    fontTools/ttLib/tables/T_S_I_D_.py
    fontTools/ttLib/tables/T_S_I_J_.py
    fontTools/ttLib/tables/T_S_I_P_.py
    fontTools/ttLib/tables/T_S_I_S_.py
    fontTools/ttLib/tables/T_S_I_V_.py
    fontTools/ttLib/tables/T_S_I__0.py
    fontTools/ttLib/tables/T_S_I__1.py
    fontTools/ttLib/tables/T_S_I__2.py
    fontTools/ttLib/tables/T_S_I__3.py
    fontTools/ttLib/tables/T_S_I__5.py
    fontTools/ttLib/tables/T_T_F_A_.py
    fontTools/ttLib/tables/TupleVariation.py
    fontTools/ttLib/tables/V_D_M_X_.py
    fontTools/ttLib/tables/V_O_R_G_.py
    fontTools/ttLib/tables/V_V_A_R_.py
    fontTools/ttLib/tables/__init__.py
    fontTools/ttLib/tables/_a_n_k_r.py
    fontTools/ttLib/tables/_a_v_a_r.py
    fontTools/ttLib/tables/_b_s_l_n.py
    fontTools/ttLib/tables/_c_i_d_g.py
    fontTools/ttLib/tables/_c_m_a_p.py
    fontTools/ttLib/tables/_c_v_a_r.py
    fontTools/ttLib/tables/_c_v_t.py
    fontTools/ttLib/tables/_f_e_a_t.py
    fontTools/ttLib/tables/_f_p_g_m.py
    fontTools/ttLib/tables/_f_v_a_r.py
    fontTools/ttLib/tables/_g_a_s_p.py
    fontTools/ttLib/tables/_g_c_i_d.py
    fontTools/ttLib/tables/_g_l_y_f.py
    fontTools/ttLib/tables/_g_v_a_r.py
    fontTools/ttLib/tables/_h_d_m_x.py
    fontTools/ttLib/tables/_h_e_a_d.py
    fontTools/ttLib/tables/_h_h_e_a.py
    fontTools/ttLib/tables/_h_m_t_x.py
    fontTools/ttLib/tables/_k_e_r_n.py
    fontTools/ttLib/tables/_l_c_a_r.py
    fontTools/ttLib/tables/_l_o_c_a.py
    fontTools/ttLib/tables/_l_t_a_g.py
    fontTools/ttLib/tables/_m_a_x_p.py
    fontTools/ttLib/tables/_m_e_t_a.py
    fontTools/ttLib/tables/_m_o_r_t.py
    fontTools/ttLib/tables/_m_o_r_x.py
    fontTools/ttLib/tables/_n_a_m_e.py
    fontTools/ttLib/tables/_o_p_b_d.py
    fontTools/ttLib/tables/_p_o_s_t.py
    fontTools/ttLib/tables/_p_r_e_p.py
    fontTools/ttLib/tables/_p_r_o_p.py
    fontTools/ttLib/tables/_s_b_i_x.py
    fontTools/ttLib/tables/_t_r_a_k.py
    fontTools/ttLib/tables/_v_h_e_a.py
    fontTools/ttLib/tables/_v_m_t_x.py
    fontTools/ttLib/tables/asciiTable.py
    fontTools/ttLib/tables/grUtils.py
    fontTools/ttLib/tables/otBase.py
    fontTools/ttLib/tables/otConverters.py
    fontTools/ttLib/tables/otData.py
    fontTools/ttLib/tables/otTables.py
    fontTools/ttLib/tables/otTraverse.py
    fontTools/ttLib/tables/sbixGlyph.py
    fontTools/ttLib/tables/sbixStrike.py
    fontTools/ttLib/tables/ttProgram.py
    fontTools/ttLib/ttCollection.py
    fontTools/ttLib/ttFont.py
    fontTools/ttLib/ttGlyphSet.py
    fontTools/ttLib/ttVisitor.py
    fontTools/ttLib/woff2.py
    fontTools/ttx.py
    fontTools/ufoLib/__init__.py
    fontTools/ufoLib/converters.py
    fontTools/ufoLib/errors.py
    fontTools/ufoLib/etree.py
    fontTools/ufoLib/filenames.py
    fontTools/ufoLib/glifLib.py
    fontTools/ufoLib/kerning.py
    fontTools/ufoLib/plistlib.py
    fontTools/ufoLib/pointPen.py
    fontTools/ufoLib/utils.py
    fontTools/ufoLib/validators.py
    fontTools/unicode.py
    fontTools/unicodedata/Blocks.py
    fontTools/unicodedata/OTTags.py
    fontTools/unicodedata/ScriptExtensions.py
    fontTools/unicodedata/Scripts.py
    fontTools/unicodedata/__init__.py
    fontTools/varLib/__init__.py
    fontTools/varLib/__main__.py
    fontTools/varLib/avar.py
    fontTools/varLib/avarPlanner.py
    fontTools/varLib/builder.py
    fontTools/varLib/cff.py
    fontTools/varLib/errors.py
    fontTools/varLib/featureVars.py
    fontTools/varLib/instancer/__init__.py
    fontTools/varLib/instancer/__main__.py
    fontTools/varLib/instancer/featureVars.py
    fontTools/varLib/instancer/names.py
    fontTools/varLib/instancer/solver.py
    fontTools/varLib/interpolatable.py
    fontTools/varLib/interpolatableHelpers.py
    fontTools/varLib/interpolatablePlot.py
    fontTools/varLib/interpolatableTestContourOrder.py
    fontTools/varLib/interpolatableTestStartingPoint.py
    fontTools/varLib/interpolate_layout.py
    fontTools/varLib/iup.py
    fontTools/varLib/merger.py
    fontTools/varLib/models.py
    fontTools/varLib/mutator.py
    fontTools/varLib/mvar.py
    fontTools/varLib/plot.py
    fontTools/varLib/stat.py
    fontTools/varLib/varStore.py
    fontTools/voltLib/__init__.py
    fontTools/voltLib/ast.py
    fontTools/voltLib/error.py
    fontTools/voltLib/lexer.py
    fontTools/voltLib/parser.py
    fontTools/voltLib/voltToFea.py
)

RESOURCE_FILES(
    PREFIX contrib/python/fonttools/
    .dist-info/METADATA
    .dist-info/entry_points.txt
    .dist-info/top_level.txt
    fontTools/misc/plistlib/py.typed
    fontTools/ttLib/tables/table_API_readme.txt
)

END()
