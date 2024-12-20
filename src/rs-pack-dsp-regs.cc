// Copyright (C) 2022 RapidSilicon
//

#include "kernel/yosys.h"
#include "kernel/modtools.h"
#include "kernel/ffinit.h"
#include "kernel/ff.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// Bits are accessed from right to left, but in GENESIS_2
// MODE_BITS are stored in Big Endian order, so we have to
// use reverse of the bit indecies for the access:
// actual bit idx = 83 --> access idx = 0
#define MODE_BITS_GENESIS2_REGISTER_INPUTS_ID 0
#define MODE_BITS_GENESIS3_REGISTER_INPUTS_ID 83
#define MODE_BITS_REGISTER_INPUTS_ID 92

#define MODE_BITS_OUTPUT_SELECT_START_ID 1
#define MODE_BITS_OUTPUT_SELECT_WIDTH 3

struct RsPackDspRegsWorker
{
    RTLIL::Module *m_module;
    SigMap m_sigmap;
    FfInitVals m_initvals;

    RsPackDspRegsWorker(RTLIL::Module *module) :
        m_module(module), m_sigmap(module), m_initvals(&m_sigmap, module) {}

    bool run_opt_clean = false;
    void merge_regout(bool gen, bool gen3, RTLIL::Cell *DFF_driven_DSP,RTLIL::Design *design){
        RTLIL::SigSpec sig_f;
        // bool _DFF_REGOUT_ = false;
        sig_f.append(RTLIL::S1);
        sig_f.append(RTLIL::S0);
        sig_f.append(RTLIL::S1);
        // bool RST_POL = true;
        RTLIL::Const DSP_RST_POL;
        DSP_RST_POL = DFF_driven_DSP->getParam(RTLIL::escape_id("DSP_RST_POL"));
        RTLIL::Const dsp_mode_bits_const = DFF_driven_DSP->getParam(RTLIL::escape_id("MODE_BITS"));
        if ((dsp_mode_bits_const[1] == RTLIL::S1 || dsp_mode_bits_const[80] == RTLIL::S1) && DSP_RST_POL.as_int()!=0 && gen == false){
            // Apply clock and reset to DSP port 
            auto CLK_PORT = (DFF_driven_DSP->getParam(ID::DSP_CLK)).decode_string();
            auto RST_PORT = (DFF_driven_DSP->getParam(ID::DSP_RST)).decode_string();
            RTLIL::SigSpec _arst_;
            RTLIL::SigSpec _rst_;
            for (auto &module : design->selected_modules()) {
                for (auto wire : module->wires()){
                    if (wire->name==CLK_PORT.c_str()){
                        DFF_driven_DSP->setPort(RTLIL::escape_id("\\clk"), wire);
                    }
                    if (wire->name==RST_PORT.c_str()){
                        _arst_ = wire;
                    }
                }

            }

            // Handle the syncronous reset by adding DFF at its input
            if (DSP_RST_POL[2] == RTLIL::S1){
                RTLIL::SigSpec rst_sync = m_module->addWire(NEW_ID,GetSize(_arst_));
                m_module->addDff(NEW_ID, DFF_driven_DSP->getPort(RTLIL::escape_id("\\clk")), _arst_, rst_sync, DSP_RST_POL[3] == RTLIL::S1);
                _rst_ = rst_sync;
            }

            if (DSP_RST_POL[3] == RTLIL::S0 && DSP_RST_POL[1] == RTLIL::S1) {
                _rst_ = m_module->Not(NEW_ID, _arst_);
                DFF_driven_DSP->setPort(RTLIL::escape_id("\\lreset"), _rst_);
            }
            else if (DSP_RST_POL[3] == RTLIL::S1 && DSP_RST_POL[1] == RTLIL::S1){
                DFF_driven_DSP->setPort(RTLIL::escape_id("\\lreset"), _arst_);
            }
            else if (DSP_RST_POL[3] == RTLIL::S0 && DSP_RST_POL[2] == RTLIL::S1){
                RTLIL::SigSpec rst_low = m_module->addWire(NEW_ID,GetSize(_rst_));
                rst_low = m_module->Not(NEW_ID, _rst_);
                DFF_driven_DSP->setPort(RTLIL::escape_id("\\lreset"), rst_low);
            }
            else{
                DFF_driven_DSP->setPort(RTLIL::escape_id("\\lreset"), _rst_);
            }
            // _DFF_REGOUT_ = true;
            // Unset the temporary parameter
            DFF_driven_DSP->unsetParam(ID::DSP_CLK);
            DFF_driven_DSP->unsetParam(ID::DSP_RST);
            DFF_driven_DSP->unsetParam(ID::DSP_RST_POL);

            // Set the MODEBITS for REGOUT
            if (DFF_driven_DSP->type.c_str() == RTLIL::escape_id("RS_DSP2")){
                DFF_driven_DSP->setPort(RTLIL::escape_id("output_select"),sig_f);
            }
            else if (DFF_driven_DSP->type.c_str() == RTLIL::escape_id("RS_DSP3")){

                // Changing RS_DSP3 MODE_BITS param with index 92, which is REGISTER_INPUTS
                dsp_mode_bits_const[MODE_BITS_OUTPUT_SELECT_START_ID] = RTLIL::S1;
                DFF_driven_DSP->setParam(RTLIL::escape_id("MODE_BITS"), dsp_mode_bits_const);
            } else {  
                if (!gen3 && DFF_driven_DSP->getPort(RTLIL::escape_id("\\load_acc")) == RTLIL::S0){
                    dsp_mode_bits_const[1] = RTLIL::S1;
                    dsp_mode_bits_const[2] = RTLIL::S0;
                    dsp_mode_bits_const[3] = RTLIL::S0;
                }
                else if (!gen3 && DFF_driven_DSP->getPort(RTLIL::escape_id("\\load_acc")) == RTLIL::S1){
                    dsp_mode_bits_const[1] = RTLIL::S1;
                    dsp_mode_bits_const[2] = RTLIL::S0;
                    dsp_mode_bits_const[3] = RTLIL::S1;
                }
                else if(gen3 && DFF_driven_DSP->getPort(RTLIL::escape_id("\\load_acc")) == RTLIL::S1){
                    dsp_mode_bits_const[80] = RTLIL::S1;
                    dsp_mode_bits_const[81] = RTLIL::S0;
                    dsp_mode_bits_const[82] = RTLIL::S0;
                }
                else {
                    dsp_mode_bits_const[80] = RTLIL::S0;
                    dsp_mode_bits_const[81] = RTLIL::S0;
                    dsp_mode_bits_const[82] = RTLIL::S1;
                }
                DFF_driven_DSP->setParam(RTLIL::escape_id("MODE_BITS"), dsp_mode_bits_const);
            }
        }
    }
    void run_scr (bool gen, bool gen3,RTLIL::Design *design) {

        std::vector<Cell *> DSP_used_cells;
        std::vector<Cell *> DFF_used_cells;
        std::vector<Cell *> ALL_cells_of_design;
// ----- A piece of code that got all DSPs, DFF, and another cells -----
        for (auto cell : m_module->selected_cells()) {
            // Get name of DSP for checking type
            std::string cell_type_str = cell->type.str();
            if (cell_type_str == RTLIL::escape_id("RS_DSP2") ||
                    cell_type_str == RTLIL::escape_id("RS_DSPX2") ||
                    cell_type_str == RTLIL::escape_id("RS_DSP3") ||
                    cell_type_str == RTLIL::escape_id("RS_DSP")) {
                //adding all DSP cells of design
                DSP_used_cells.push_back(cell);
                // if cell type not DSP or DFF add as other cell
                ALL_cells_of_design.push_back(cell);
                continue;
            }
            if (RTLIL::builtin_ff_cell_types().count(cell->type)) {
                //adding all DFF cells of design
                DFF_used_cells.push_back(cell);
                continue;
            }
            // if cell type not DSP or DFF add as other cell
            ALL_cells_of_design.push_back(cell);
        }

// ----- A piece of code that filters all DSP leaving only those with only DFF in the input  -----
        std::vector <RTLIL::Cell*> DSP_driven_only_by_DFF;
        std::vector <RTLIL::Cell*> DSP_drives_DFF;
        RTLIL::SigSpec DFF_clk;
        RTLIL::SigSpec DFF_rst;
        bool DFF_hasArst = false;
        bool DFF_hasSrst = false;
        bool DFF_ARST_POL = true;
        bool DFF_SRST_POL = true;
        bool DFF_REGOUT = false;
        // Getting each DSP from all DSPs of our MODULE
        for (auto &it_dsp : DSP_used_cells) {
            log_debug("Working with DSP by name: %s.\n", it_dsp->name.c_str());

            // if the port of DSP driven from DFF
            bool port_a_from_dff = false;
            bool port_b_from_dff = false;
            bool port_load_acc_from_dff = false;
            // if we check somthing and need to ignore selected DSP
            bool ignore_dsp = false;
            // if we check somthing and need to ignore selected DFF
            bool next_dff = false;
            // Get RESET and CLK SigSpec to compare with another DFF RESET and CLK SigSpecs which drive DSP signals
            bool for_first_dff = true;
            // get it_dsp SigSpec of ports (a) and (b)
            RTLIL::SigSpec DSP_port_a = it_dsp->getPort(RTLIL::escape_id("\\a"));
            RTLIL::SigSpec DSP_port_b = it_dsp->getPort(RTLIL::escape_id("\\b"));
            RTLIL::SigSpec DSP_port_load_acc = it_dsp->getPort(RTLIL::escape_id("\\load_acc"));

            // Getting each cell of DESIGN to check if there is connection between
            for (auto &it_cell : ALL_cells_of_design) {
                next_dff = false;
                // skip if cell is the same DSP
                if (it_cell->name == it_dsp->name)
                    continue;
                // Getting all connections of each cell
                for (auto &conn_cell : it_cell->connections_) {
                    if (it_cell->output(conn_cell.first)) {
                        // Checking if one of the Ports of DSP is driven from other cell
                        for (auto bit_cell : m_sigmap(conn_cell.second)) {
                            for (auto bit_dsp : m_sigmap(DSP_port_a)) {
                                if (bit_cell == bit_dsp) {
                                    log_debug("There is a connection between DSP port ( \\a ) and Cell port ( %s )\n", conn_cell.first.c_str());
                                    ignore_dsp = true;
                                    // exit DSP bits loop
                                    break;
                                }
                            }
                            // exit cell bits loop
                            if (ignore_dsp)
                                break;
                            for (auto bit_dsp : m_sigmap(DSP_port_b)) {
                                if (bit_cell == bit_dsp) {
                                    log_debug("There is a connection between DSP port ( \\b ) and Cell port ( %s )\n", conn_cell.first.c_str());
                                    ignore_dsp = true;
                                    // exit DSP bits loop
                                    break;
                                }
                            }
                            // exit cell bits loop
                            if (ignore_dsp)
                                break;
                        }
                        // exit cell connections loop
                        if (ignore_dsp)
                            break;
                    }
                }
                // exit cells loop
                if (ignore_dsp)
                    break;
            }
            // pick next DSP
            if (ignore_dsp){
                if (!gen)
                    merge_regout(gen,gen3,it_dsp,design);
                continue;
            }
            // Getting each DFF from all DFFs of our MODULE
            for (auto &it_dff : DFF_used_cells) {
                log_debug("Working with DFF by name: %s.\n", it_dff->name.c_str());
                FfData ff(&m_initvals, it_dff);

                // Lambda function for action when having connection between DSP and DFF
                auto check_dff = [&DFF_hasArst, &DFF_hasSrst, &DFF_ARST_POL, &DFF_rst, &DFF_clk, &for_first_dff, &ff, &ignore_dsp, &next_dff, &DFF_SRST_POL, this](bool &port_from_dff, string working_port) {
                    log_debug("There is a connection between DSP port ( \\%s ) and DFF port ( q )\n", working_port.c_str());
                    // EDA-1701: if reset value is not zero ignore DSP
                    if (ff.has_ce || ff.has_sr || ff.has_aload || ff.has_gclk || !ff.has_clk || ff.val_srst.as_int()>0 || ff.val_arst.as_int() > 0 ) {
                        ignore_dsp = true;
                        return;
                    }
                    if (for_first_dff) {
                        // Getting selected DFF RESET and CLOCK SigSepc
                        DFF_hasArst = ff.has_arst;
                        DFF_hasSrst = ff.has_srst;
                        DFF_ARST_POL = ff.pol_arst;
                        DFF_SRST_POL = ff.pol_srst;
                        if (DFF_hasArst)
                            DFF_rst = ff.sig_arst;
                        if (DFF_hasSrst)
                            DFF_rst = ff.sig_srst;
                        DFF_clk = ff.sig_clk;
                        // if DSP port is driven from DFF make it true
                        port_from_dff = true;
                        // first time desable
                        for_first_dff = false;
                        // pick next dff
                        next_dff = false;
                    } else {
                        if (DFF_hasArst != ff.has_arst ||
                            DFF_hasSrst != ff.has_srst ||
                            ff.sig_clk != m_sigmap(DFF_clk)) {
                            ignore_dsp = true;
                            return;
                        }
                        if (DFF_hasArst) {
                            if (ff.sig_arst != m_sigmap(DFF_rst)) {
                                ignore_dsp = true;
                                return;
                            }
                        }
                        if (DFF_hasSrst) {
                            if (ff.sig_srst != m_sigmap(DFF_rst)) {
                                ignore_dsp = true;
                                return;
                            }
                        }
                        port_from_dff = true;
                        next_dff = false;
                    }
                };


                for (auto bit_dff : m_sigmap(ff.sig_q)) {
                    // getting all bits of DSP port (a)
                    for (auto bit_dsp : m_sigmap(DSP_port_a)) {
                        // comparing if DFF port bit is the same as DSP port bit
                        if (bit_dff == bit_dsp) {
                            // calling lambda function for port (a)
                            check_dff(port_a_from_dff, "a");
                            // we cancel the comparison of the remaining bits because it is already clear that the DSP port receives a signal from the DFF
                            break;
                        }
                    }
                    if (ignore_dsp)
                        break;
                    // getting all bits of DSP port (b)
                    for (auto bit_dsp : m_sigmap(DSP_port_b)) {
                        // comparing if DFF port bit is the same as DSP port bit
                        if (bit_dff == bit_dsp) {
                            // calling lambda function for port (a)
                            check_dff(port_b_from_dff, "b");
                            // we cancel the comparison of the remaining bits because it is already clear that the DSP port receives a signal from the DFF
                            break;
                        }
                    }
                    if(DSP_port_load_acc.is_fully_const()){
                        port_load_acc_from_dff = true;
                    }
                    else{
                        for (auto bit_dsp : m_sigmap(DSP_port_load_acc)) {
                            // comparing if DFF port bit is the same as DSP port bit
                            if (bit_dff == bit_dsp) {
                                // calling lambda function for port (load_acc)
                                check_dff(port_load_acc_from_dff, "load_acc");
                                if (port_load_acc_from_dff){
                                    RTLIL::SigSpec new_sigspec_for_load_acc;

                                    RTLIL::SigSpec DSP_port_load_acc = it_dsp->getPort(RTLIL::escape_id("\\load_acc"));

                                    for (auto bit_dsp : m_sigmap(DSP_port_load_acc)) {
                                        // get all DFFs
                                        for (auto &it_dff : DFF_used_cells) {
                                            // making each DFF as FF object
                                            FfData ff(&m_initvals, it_dff);
                                            // this index var is used to take the input bit of DFF with the index of the output bit DFF
                                            int use_index_for_dff = 0;
                                            // get all bits of DFF output port
                                            for (auto bit_dff : m_sigmap(ff.sig_q)) {
                                                // compare if bit of DSP port (b) is the same with bit of DFF port (q)
                                                if (bit_dsp == bit_dff) {
                                                    // add DFF input data port bit for new SigSpec for DSP port (b)
                                                    new_sigspec_for_load_acc.append(ff.sig_d[use_index_for_dff]);
                                                    // get selected DFF clock and reset for this DSP
                                                    DFF_clk = ff.sig_clk;
                                                    DFF_hasArst = ff.has_arst;
                                                    DFF_hasSrst = ff.has_srst;
                                                    if (DFF_hasArst)
                                                        DFF_rst = ff.sig_arst;
                                                    if (DFF_hasSrst)
                                                        DFF_rst = ff.sig_srst;
                                                }
                                                // incrementing index;
                                                use_index_for_dff++;
                                            }
                                        }
                                    }
                                    it_dsp->setPort(RTLIL::escape_id("\\load_acc"), new_sigspec_for_load_acc);
                                }

                                // we cancel the comparison of the remaining bits because it is already clear that the DSP port receives a signal from the DFF
                                break;
                            }
                        }
                    }
                    if (ignore_dsp)
                        break;
                    if (next_dff || ignore_dsp)
                        break;
                }
                if (ignore_dsp)
                    break;
                if (next_dff) {
                    continue;
                }
            }
            RTLIL::Const DSP_RST_POL;
            if (!gen)
            {
                DSP_RST_POL = it_dsp->getParam(RTLIL::escape_id("DSP_RST_POL"));
                if ((ignore_dsp) && !(DSP_RST_POL.as_int()))
                    continue;
                // if DSP data ports is driven from DFFs add it in vector
                if (port_a_from_dff && port_b_from_dff && port_load_acc_from_dff && (!ignore_dsp || DSP_RST_POL.as_int() != 0))
                    DSP_driven_only_by_DFF.push_back(it_dsp);
            }
            else
            {
                if (ignore_dsp)
                    continue;
                // if DSP data ports is driven from DFFs add it in vector
                if (port_a_from_dff && port_b_from_dff && port_load_acc_from_dff && !ignore_dsp)
                    DSP_driven_only_by_DFF.push_back(it_dsp);
            }

            // if DSP data ports is driven from DFFs add it in vector

            if (1)
            {
                DSP_drives_DFF.push_back(it_dsp);
            }
        }

        if(!gen){
            for (auto &DFF_driven_DSP : DSP_drives_DFF) {
               merge_regout(gen,gen3,DFF_driven_DSP,design);
            }
        }  
    

// ----- A piece of code that works with DSP connections  -----
        for (auto &DSP_driven_DFF : DSP_driven_only_by_DFF) {
            // creating new SigSpecs with which we will change the SigSpecs of DSP ports A and B
            RTLIL::SigSpec new_sigspec_for_a;
            RTLIL::SigSpec new_sigspec_for_b;
            RTLIL::SigSpec new_sigspec_for_load_acc;

            // get selected DSP SigSpec of ports (a) and (b)
            RTLIL::SigSpec DSP_port_a = DSP_driven_DFF->getPort(RTLIL::escape_id("\\a"));
            RTLIL::SigSpec DSP_port_b = DSP_driven_DFF->getPort(RTLIL::escape_id("\\b"));
            RTLIL::SigSpec DSP_port_load_acc = DSP_driven_DFF->getPort(RTLIL::escape_id("\\load_acc"));

            // get all bits of DSP port (a)
            for (auto bit_dsp : m_sigmap(DSP_port_a)) {
                // get all DFFs
                for (auto &it_dff : DFF_used_cells) {
                    // making each DFF as FF object
                    FfData ff(&m_initvals, it_dff);
                    // this index var is used to take the input bit of DFF with the index of the output bit DFF
                    int use_index_for_dff = 0;
                    // get all bits of DFF output port
                    for (auto bit_dff : m_sigmap(ff.sig_q)) {
                        // compare if bit of DSP port (a) is the same with bit of DFF port (q)
                        if (bit_dsp == bit_dff) {
                            // add DFF input data port bit for new SigSpec for DSP port (a)
                            new_sigspec_for_a.append(ff.sig_d[use_index_for_dff]);
                            // get selected DFF clock and reset for this DSP
                            DFF_clk = ff.sig_clk;
                            DFF_hasArst = ff.has_arst;
                            DFF_hasSrst = ff.has_srst;
                            if (DFF_hasArst)
                                DFF_rst = ff.sig_arst;
                            if (DFF_hasSrst)
                                DFF_rst = ff.sig_srst;
                        }
                        // incrementing index;
                        use_index_for_dff++;
                    }
                }
            }

            // get all bits of DSP port (b)
            for (auto bit_dsp : m_sigmap(DSP_port_b)) {
                // get all DFFs
                for (auto &it_dff : DFF_used_cells) {
                    // making each DFF as FF object
                    FfData ff(&m_initvals, it_dff);
                    // this index var is used to take the input bit of DFF with the index of the output bit DFF
                    int use_index_for_dff = 0;
                    // get all bits of DFF output port
                    for (auto bit_dff : m_sigmap(ff.sig_q)) {
                        // compare if bit of DSP port (b) is the same with bit of DFF port (q)
                        if (bit_dsp == bit_dff) {
                            // add DFF input data port bit for new SigSpec for DSP port (b)
                            new_sigspec_for_b.append(ff.sig_d[use_index_for_dff]);
                            // get selected DFF clock and reset for this DSP
                            DFF_clk = ff.sig_clk;
                            DFF_hasArst = ff.has_arst;
                            DFF_hasSrst = ff.has_srst;
                            if (DFF_hasArst)
                                DFF_rst = ff.sig_arst;
                            if (DFF_hasSrst)
                                DFF_rst = ff.sig_srst;
                        }
                        // incrementing index;
                        use_index_for_dff++;
                    }
                }
            }
            // After all DFFs will be changed DSP port (a) and (b)
            DSP_driven_DFF->setPort(RTLIL::escape_id("\\a"), new_sigspec_for_a);
            DSP_driven_DFF->setPort(RTLIL::escape_id("\\b"), new_sigspec_for_b);

            // Getting DSP Reginster inputs port to change value 1
            if (DSP_driven_DFF->type.c_str() == RTLIL::escape_id("RS_DSP2"))
                DSP_driven_DFF->setPort(RTLIL::escape_id("register_inputs"), RTLIL::S1);
            else if (DSP_driven_DFF->type.c_str() == RTLIL::escape_id("RS_DSP3")){
                // Getting RS_DSP3 MODE_BITS param;
                RTLIL::Const dsp_mode_bits_const = DSP_driven_DFF->getParam(RTLIL::escape_id("MODE_BITS"));
                // Changing RS_DSP3 MODE_BITS param with index 92, which is REGISTER_INPUTS
                dsp_mode_bits_const[MODE_BITS_REGISTER_INPUTS_ID] = RTLIL::S1;
                DSP_driven_DFF->setParam(RTLIL::escape_id("MODE_BITS"), dsp_mode_bits_const);
            } else {
                // Getting RS_DSP MODE_BITS param;
                RTLIL::Const dsp_mode_bits_const = DSP_driven_DFF->getParam(RTLIL::escape_id("MODE_BITS"));
                // Changing RS_DSP MODE_BITS param with index 83, which is REGISTER_INPUTS
                if (!gen3)
                    dsp_mode_bits_const[MODE_BITS_GENESIS2_REGISTER_INPUTS_ID] = RTLIL::S1;
                else
                    dsp_mode_bits_const[MODE_BITS_GENESIS3_REGISTER_INPUTS_ID] = RTLIL::S1;

                DSP_driven_DFF->setParam(RTLIL::escape_id("MODE_BITS"), dsp_mode_bits_const);
            }

            // Getting DSP clock port to connect it with DFF clock port
            DSP_driven_DFF->setPort(RTLIL::escape_id("\\clk"), DFF_clk);
            // Getting DSP reset port to connect it with DFF reset port
            if (DFF_REGOUT == false){
                RTLIL::SigSpec _arst_;
                bool rst_inv = false;
                if (DFF_hasArst || DFF_hasSrst) {
                    // BEGIN: Awais: inverter added at ouput of reset as active low rest is not supported by DSP architecture.
                    if ((DFF_ARST_POL == 0  and DFF_hasArst) || (DFF_SRST_POL == 0  and DFF_hasSrst)){
                        rst_inv = true;
                        _arst_ = m_module->Not(NEW_ID, DFF_rst);
                    }
                    if (((DSP_driven_DFF->type.c_str() == RTLIL::escape_id("RS_DSP")) || (DSP_driven_DFF->type.c_str() == RTLIL::escape_id("RS_DSPX2"))) and rst_inv){
                        DSP_driven_DFF->setPort(RTLIL::escape_id("\\lreset"), _arst_);
                    }
                    // END: Awais: inverter added at ouput of reset as active low rest is not supported by DSP architecture.
                    else if (((DSP_driven_DFF->type.c_str() == RTLIL::escape_id("RS_DSP")) || (DSP_driven_DFF->type.c_str() == RTLIL::escape_id("RS_DSPX2"))) and rst_inv == 0){
                        DSP_driven_DFF->setPort(RTLIL::escape_id("\\lreset"), DFF_rst);
                    }
                    else{
                        DSP_driven_DFF->setPort(RTLIL::escape_id("\\reset"), DFF_rst);
                    }
                }
            }
            run_opt_clean = true;
        }

    }
};

struct RsPackDspRegsPass : public Pass {
    RsPackDspRegsPass() : Pass("rs-pack-dsp-regs", "pack DSP input registers") { }
    void help() override
    {
        //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
        log("\n");
        log("    Rs_pack_dsp_regs [selection]\n");
        log("\n");
        log("    This pass packs DSP input registers inside DSP component\n");
        log("\n");
    }

    void execute(std::vector<std::string> a_Args, RTLIL::Design *design) override
    {
        log_header(design, "Executing rs_pack_dsp_regs pass.\n");
        bool gen3 = false;
        bool gen = false;
        size_t argidx;
        for (argidx = 1; argidx < a_Args.size(); argidx++) {
            if (a_Args[argidx] == "-genesis3")
                gen3 = true;
            if (a_Args[argidx] == "-genesis")
                gen = true;
        }
        
        extra_args(a_Args, argidx, design);

        for (auto mod : design->selected_modules()) {
            RsPackDspRegsWorker worker(mod);
            worker.run_scr(gen,gen3,design);
            if (worker.run_opt_clean)
                Pass::call(design, "opt_clean");
        }
    }
} RsPackDspRegsPass;

PRIVATE_NAMESPACE_END
