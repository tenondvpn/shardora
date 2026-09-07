#pragma once

#include <string>
#include "explorer/explorer.h"

// UWS request/response wrappers (same as http_handler.cc)
namespace shardora {
namespace init {
struct UWSRequest;
struct UWSResponse;
}  // namespace init

namespace explorer {

// HTTP handler functions — registered as GET /explorer/* routes
// Each function reads query params, calls explorer_ methods, writes JSON response.

void ExplorerBlocks     (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerBlock      (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerTxList     (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerTx         (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerAddress    (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerAddressTxs (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerContracts  (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerContract   (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerGasPresets (const init::UWSRequest& req, init::UWSResponse& res);
void ExplorerChainInfo  (const init::UWSRequest& req, init::UWSResponse& res);

// Global explorer instance — set by NetworkInit, used by handlers
void SetGlobalExplorer(std::shared_ptr<Explorer> explorer);

}  // namespace explorer
}  // namespace shardora
