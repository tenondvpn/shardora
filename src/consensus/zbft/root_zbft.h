#pragma once

#include "consensus/zbft/zbft.h"

namespace zjchain {

namespace elect {
    class ElectManager;
}

namespace consensus {

class RootZbft : public Zbft {
public:
    RootZbft(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr, 
        std::shared_ptr<WaitingTxsItem>& tx_ptr,
        std::shared_ptr<consensus::WaitingTxsPools>& pools_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr);
    virtual ~RootZbft();
    virtual void DoTransactionAndCreateTxBlock(block::protobuf::Block& zjc_block);
    void RootDefaultTx(block::protobuf::Block& zjc_block);
    void RootCreateAccountAddressBlock(block::protobuf::Block& zjc_block);
    void RootCreateElectConsensusShardBlock(block::protobuf::Block& zjc_block);

private:

    DISALLOW_COPY_AND_ASSIGN(RootZbft);
};

};  // namespace consensus

};  // namespace zjchain