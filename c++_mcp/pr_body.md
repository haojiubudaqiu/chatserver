## Description
This PR introduces support for the `prompts` primitive defined in the Model Context Protocol (MCP) specification. 

Prompts allow servers to expose prompt templates (with structured arguments) to human-in-the-loop clients or AI agents, enabling complex, standardized workflows natively through the MCP standard.

### Changes Included:
- **`mcp_prompt.h`**: Added abstraction definitions for `prompt` and `prompt_argument`, including a fluent `prompt_builder` for simple prompt creation.
- **`mcp_server.h/cpp`**: 
  - Added `prompts_` registry and `register_prompt` method.
  - Implemented `prompts/list` to expose available prompt templates to clients.
  - Implemented `prompts/get` to handle prompt execution, parameter parsing, and return workflow messages (`role`, `content`).
- **`README.md`**: Documented how to register and use Prompts, matching the existing documentation style for Resources and Tools.
- **Testing**: Added unit tests for prompt registration, list, and get functions in the `mcp_tests` suite.

## Motivation
Prior to this PR, `cpp-mcp` supported Tools and Resources natively but was missing the third core pillar of the MCP standard: Prompts. This addition allows `cpp-mcp` servers to seamlessly integrate with rich MCP clients (like Claude Desktop, Cursor, OpenCode) that utilize prompt templates.

## Testing
- Verified `prompts/list` correctly returns the JSON schema matching the protocol spec.
- Verified `prompts/get` correctly maps incoming `arguments`, triggers the associated C++ handler, and accurately propagates the returned standard message array back to the client.
- Added tests to `test/mcp_server_test.cpp` and ensured all tests pass (`ctest`).
- Confirmed stable operation across Stdio and SSE transports.
