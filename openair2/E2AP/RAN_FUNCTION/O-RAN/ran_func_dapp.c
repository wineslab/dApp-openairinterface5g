#include "ran_func_dapp.h"

static pthread_once_t once_dapp_mutex = PTHREAD_ONCE_INIT;
static dapp_subs_data_t dapp_subs_data = {0};

/**
 * @brief Create a byte_array_t from a C-string.
 *
 * Allocates a new buffer, copies the content of @p s (without NUL terminator),
 * and returns a byte_array_t owning that buffer.
 */
static byte_array_t ba_from_cstr(const char* s)
{
  byte_array_t ba = {0};
  size_t len = strlen(s);
  ba.buf = malloc(len);
  assert(ba.buf != NULL && "Memory exhausted");
  memcpy(ba.buf, s, len);
  ba.len = len;
  return ba;
}

/**
 * @brief Fill the DAPP RAN function name/description/oid fields.
 *
 * Initializes the name, OID, and description of the given DAPP function
 * definition using constant strings; leaves instance as NULL.
 */
static void fill_dapp_ran_function_name(e2sm_dapp_func_def_t* def)
{
  assert(def != NULL);

  def->name.name = ba_from_cstr("E2SM-DAPP");

  def->name.oid = ba_from_cstr("1.3.6.1.4.1.53148.1.1.255.3");

  def->name.description = ba_from_cstr("E2SM-DAPP for xApp-dApp synchronization");

  def->name.instance = NULL;
}

/**
 * @brief Build the DAPP report style definition.
 *
 * Allocates and initializes a ran_func_def_report_dapp_sm_t with a single
 * report style (style 1) suitable for use in the DAPP RAN function definition.
 */
static ran_func_def_report_dapp_sm_t* make_dapp_report_def(void)
{
  ran_func_def_report_dapp_sm_t* rpt = calloc(1, sizeof(*rpt));
  assert(rpt != NULL && "Memory exhausted");

  rpt->sz_seq_report_sty = 1;
  rpt->seq_report_sty = calloc(rpt->sz_seq_report_sty, sizeof(seq_report_sty_dapp_sm_t));
  assert(rpt->seq_report_sty != NULL && "Memory exhausted");

  seq_report_sty_dapp_sm_t* s = &rpt->seq_report_sty[0];

  s->report_type = 1;
  s->name = ba_from_cstr("DAPP-REPORT-STYLE-1");
  s->ev_trig_type = 1;
  s->act_frmt_type = 1;
  s->ind_hdr_type = 1;
  s->ind_msg_type = 1;

  return rpt;
}

/**
 * @brief Build the DAPP control style definition.
 *
 * Allocates and initializes a ran_func_def_ctrl_dapp_sm_t with a single
 * control style (style 1) for the DAPP RAN function definition.
 */
static ran_func_def_ctrl_dapp_sm_t* make_dapp_ctrl_def(void)
{
  ran_func_def_ctrl_dapp_sm_t* ctrl = calloc(1, sizeof(*ctrl));
  assert(ctrl != NULL && "Memory exhausted");

  ctrl->sz_seq_ctrl_style = 1;
  ctrl->seq_ctrl_style = calloc(ctrl->sz_seq_ctrl_style, sizeof(seq_ctrl_style_dapp_sm_t));
  assert(ctrl->seq_ctrl_style != NULL && "Memory exhausted");

  seq_ctrl_style_dapp_sm_t* s = &ctrl->seq_ctrl_style[0];

  s->style_type = 1;
  s->name = ba_from_cstr("DAPP-CONTROL-STYLE-1");
  s->hdr = 1;
  s->msg = 1;
  s->out_frmt = 1;

  return ctrl;
}

static void init_once_dapp(void)
{
  init_dapp_subs_data(&dapp_subs_data);
}

/**
 * @brief Build the full DAPP RAN function definition.
 *
 * Creates a local e2sm_dapp_func_def_t with:
 *  - name/description/oid filled,
 *  - NULL event trigger definition,
 *  - one report style and one control style.
 */
static e2sm_dapp_func_def_t fill_dapp_ran_def(void)
{
  e2sm_dapp_func_def_t def = (e2sm_dapp_func_def_t){0};

  fill_dapp_ran_function_name(&def);

  def.ev_trig = NULL;

  def.report = make_dapp_report_def();

  def.ctrl = make_dapp_ctrl_def();

  return def;
}

/**
 * @brief Populate the DAPP RAN function definition during E2 setup.
 *
 * This is called by the E2 agent when building the DAPP RAN function
 * setup structure. It:
 *  - Fills dapp->ran_func_def with the DAPP RAN function definition.
 *  - Ensures global DAPP subscription state is initialized once.
 */
void read_dapp_setup_sm(void* data)
{
  assert(data != NULL);

  dapp_e2_setup_t* dapp = (dapp_e2_setup_t*)data;

  dapp->ran_func_def = fill_dapp_ran_def();

  const int ret = pthread_once(&once_dapp_mutex, init_once_dapp);
  DevAssert(ret == 0);
}

/**
 * @brief Convert an E3 DAPP report into one or more E2 indications.
 *
 * For each RIC subscription currently registered:
 *  - Allocates a dapp_ind_data_t,
 *  - Fills the E2SM-DAPP IndicationHeader (RAN function ID, dApp ID),
 *  - Wraps the E3 report bytes in the IndicationMessage format 0 payload,
 *  - Enqueues the indication on the FlexRIC async path.
 *
 * If @p report_data is NULL or @p report_size == 0, it returns immediately.
 */
void generate_e2_indication_from_e3_dapp_report(uint32_t ran_function_id,
                                                uint32_t dapp_id,
                                                size_t report_size,
                                                const uint8_t* report_data)
{
  if (report_data == NULL || report_size == 0) {
    return;
  }

  uint32_t *ric_ids = NULL;
  size_t count = ric_subs_snapshot(&ric_ids);
  if (count == 0 || ric_ids == NULL) {
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    uint32_t ric_req_id = ric_ids[i];

    dapp_ind_data_t* ind = calloc(1, sizeof(*ind));
    assert(ind);

    /* -------- Indication Header -------- */
    ind->hdr.format = FORMAT_0_E2SM_DAPP_IND_HDR;
    e2sm_dapp_ind_hdr_frmt_0_t* hdr = &ind->hdr.frmt_0;
    hdr->ran_function_id = ran_function_id;
    hdr->dapp_id = dapp_id;

    /* -------- Indication Message -------- */
    ind->msg.format = FORMAT_0_E2SM_DAPP_IND_MSG;
    e2sm_dapp_ind_msg_frmt_0_t* msg = &ind->msg.frmt_0;

    msg->data_size = report_size;
    msg->data = malloc(report_size);
    assert(msg->data != NULL);
    memcpy(msg->data, report_data, report_size);

    /* Push to FlexRIC async path */
    async_event_agent_api(ric_req_id, ind);
  }

  free(ric_ids);
}

/**
 * @brief Callback invoked when an aperiodic subscription is released.
 *
 * Removes the given RIC request ID from the internal DAPP subscription
 * bookkeeping.
 */
static void free_aperiodic_subscription(uint32_t ric_req_id)
{
  remove_dapp_subs_data(&dapp_subs_data, ric_req_id);
}

/**
 * @brief Handle DAPP SM subscription requests at the agent.
 *
 * Called when the RIC installs a subscription for the DAPP SM. For
 * supported ActionDefinition formats (currently FORMAT_0 only), it:
 *  - Stores the RIC request ID in DAPP subscription data,
 *  - Registers the RIC subscription,
 *  - Returns an aperiodic subscription outcome with a custom free callback.
 */
sm_ag_if_ans_t write_subs_dapp_sm(void const* src)
{
  assert(src != NULL);
  wr_dapp_sub_data_t* wr_dapp = (wr_dapp_sub_data_t*)src;
  assert(wr_dapp->dapp.ad != NULL);

  sm_ag_if_ans_t ans = {0};
  const uint32_t ric_req_id = wr_dapp->ric_req_id;

  switch (wr_dapp->dapp.ad->format) {
    case FORMAT_0_E2SM_DAPP_ACT_DEF: {
      insert_fmt_0_ric_id(&dapp_subs_data, ric_req_id);
      ric_subs_add(ric_req_id);

      ans.type = SUBS_OUTCOME_SM_AG_IF_ANS_V0;
      ans.subs_out.type = APERIODIC_SUBSCRIPTION_FLRC;
      ans.subs_out.aper.free_aper_subs = free_aperiodic_subscription;
      break;
    }

    default:
      AssertError(false, return ans, "[RAN FUNC DAPP SUBS] Unsupported Action Definition format %d", wr_dapp->dapp.ad->format);
  }

  return ans;
}

/**
 * @brief Handle DAPP SM control requests at the agent.
 *
 * For format-0 E2SM-DAPP ControlMessages, and when compiled with E3_AGENT:
 *  - Extracts RAN function ID, dApp ID, and control payload,
 *  - Forwards it to the E3 agent via libe3 C API wrapper.
 *
 * Always returns a control outcome of type DAPP_AGENT_IF_CTRL_ANS_V0.
 */
sm_ag_if_ans_t write_ctrl_dapp_sm(void const* data)
{
  assert(data != NULL);

  dapp_ctrl_req_data_t const* ctrl = (dapp_ctrl_req_data_t const*)data;
  sm_ag_if_ans_t ans = {0};

  switch (ctrl->msg.format) {
    case FORMAT_0_E2SM_DAPP_CTRL_MSG: {
#if defined(E3_AGENT)
      uint32_t ran_function_id = ctrl->hdr.frmt_0.ran_function_id;
      uint32_t dapp_id = ctrl->hdr.frmt_0.dapp_id;

      uint32_t data_size = ctrl->msg.frmt_0.data_size;
      const uint8_t* payload = ctrl->msg.frmt_0.data;

      if (e3_send_xapp_control(dapp_id, ran_function_id, payload, data_size) != 0) {
        printf("[RAN FUNC DAPP CTRL] Failed to forward xApp control via E3 agent\n");
      }
#endif

      break;
    }

    default:
      AssertError(false, return ans, "[RAN FUNC DAPP CTRL] Unsupported Control Message format %d", ctrl->msg.format);
  }

  ans.type = CTRL_OUTCOME_SM_AG_IF_ANS_V0;
  ans.ctrl_out.type = DAPP_AGENT_IF_CTRL_ANS_V0;
  return ans;
}

/**
 * @brief Read DAPP SM state from the agent (not implemented).
 *
 * Placeholder for a future read operation for the DAPP SM. Currently it
 * asserts unconditionally and returns true to satisfy the interface.
 */
bool read_dapp_sm(void* data)
{
  assert(data != NULL);
  assert(0 != 0 && "Not implemented");

  return true;
}
