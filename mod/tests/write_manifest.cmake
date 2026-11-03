string(REPLACE "\"" "" WITNESS "${WITNESS}")
string(REPLACE "\"" "" PROXY "${PROXY}")
file(SHA256 "${WITNESS}" WITNESS_SHA256)
file(SHA256 "${PROXY}" PROXY_SHA256)
get_filename_component(OUTPUT_DIR "${WITNESS}" DIRECTORY)
file(WRITE "${OUTPUT_DIR}/tdur-observation-manifest.json"
"{\n  \"schema\": \"tdur-observation-v1\",\n  \"architecture\": \"x86\",\n  \"proxy\": \"dinput8.dll\",\n  \"proxy_sha256\": \"${PROXY_SHA256}\",\n  \"witness\": \"tdur_mod_witness.exe\",\n  \"witness_sha256\": \"${WITNESS_SHA256}\",\n  \"expected_proxy_exports\": [\"DirectInput8Create\"],\n  \"reference_game_sha256\": \"3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30\",\n  \"game_executed\": false,\n  \"requires_external_save_locks\": true,\n  \"requires_external_job_and_slot\": true\n}\n")
