"""冻结 Reader 协议。普通 VS 构建使用已冻结文件。"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
from google.protobuf import descriptor_pb2

root = Path(__file__).resolve().parent.parent
version = 2
directory = f"reader/v{version}"
files = [f"{directory}/{name}" for name in ("model.proto", "snapshot.proto", "reader.proto")]
destination = root / "protocol" / directory
with tempfile.TemporaryDirectory(prefix="cs2-protocol-") as temporary:
    output = Path(temporary) / "descriptor.pb"
    subprocess.run([sys.executable, "-m", "grpc_tools.protoc", "-I", str(root / "protocol"),
                    "--include_imports", "--descriptor_set_out=" + str(output), *files], check=True)
    source = descriptor_pb2.FileDescriptorSet.FromString(output.read_bytes())
    descriptor = descriptor_pb2.FileDescriptorSet()
    for item in sorted(source.file, key=lambda item: item.name):
        target = descriptor.file.add()
        target.CopyFrom(item)
        target.ClearField("source_code_info")
    data = descriptor.SerializeToString(deterministic=True)
digest = hashlib.sha256(data).digest()
descriptor_name = f"reader-v{version}.descriptor.pb"
(destination / descriptor_name).write_bytes(data)
header = root / f"sdk/include/cs2_reader/v{version}/schema.hpp"
header.write_text("#pragma once\n#include <array>\n#include <cstddef>\n#include <cstdint>\n#include <string_view>\n"
    "// 由build/freeze_protocol.py生成；摘要来自随协议冻结的descriptor。\n"
    f"namespace cs2_reader::v{version} {{\ninline constexpr std::uint32_t protocol_version = {version};\n"
    "inline constexpr std::array<std::byte,32> schema_sha256 = {\n    "
    + ",".join("std::byte{0x%02x}" % value for value in digest) + "\n};\n"
    + 'inline constexpr std::string_view schema_sha256_hex = "' + digest.hex() + '";\n}\n', encoding="utf-8")
manifest = {"protocol": f"cs2.reader.v{version}", "protocol_version": version, "schema_sha256": digest.hex(),
            "descriptor": descriptor_name, "sources": {
                file: hashlib.sha256((root / "protocol" / file).read_bytes()).hexdigest() for file in files}}
(destination / "schema-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(digest.hex())
