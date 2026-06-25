# protobuf version: 3.6.1
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"${SCRIPT_DIR}/../third_party/bin/protoc" --proto_path="${SCRIPT_DIR}" "${SCRIPT_DIR}"/protos/*.proto --cpp_out="${SCRIPT_DIR}"
"${SCRIPT_DIR}/../third_party/bin/protoc" --proto_path="${SCRIPT_DIR}" --python_out="${SCRIPT_DIR}" "${SCRIPT_DIR}"/protos/*.proto
