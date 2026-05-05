class PromptsEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Set up test environment
        server::configuration conf = {.host = "localhost", .port = 8084};
        server_ = std::make_unique<server>(conf);
        
        // Create a test prompt
        prompt test_prompt = prompt_builder("test_prompt")
            .with_description("A test prompt")
            .with_argument("name", "The user name", true)
            .build();
        
        // Register prompt
        server_->register_prompt(test_prompt, [](const json& params, const std::string& /* session_id */) -> json {
            std::string name = "World";
            if (params.contains("name")) {
                name = params["name"].get<std::string>();
            }
            
            return json::array({
                {
                    {"role", "user"},
                    {"content", {
                        {"type", "text"},
                        {"text", "Hello, " + name + "!"}
                    }}
                }
            });
        });
        
        // Start server in background thread
        server_thread_ = std::thread([this]() {
            server_->start();
        });
        
        // Wait for server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Connect client
        client_ = std::make_unique<sse_client>("http://localhost:8084");
        bool initialized = client_->initialize("TestClient", "1.0");
        EXPECT_TRUE(initialized);
    }

    void TearDown() override {
        if (client_) {
            client_.reset();
        }
        if (server_) {
            server_->stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    static std::shared_ptr<sse_client>& GetClient() {
        return client_;
    }

private:
    std::unique_ptr<server> server_;
    std::thread server_thread_;
    static std::shared_ptr<sse_client> client_;
};

std::shared_ptr<sse_client> PromptsEnvironment::client_ = nullptr;

class PromptsTest : public ::testing::Test {
protected:
    void SetUp() override {
        client_ = PromptsEnvironment::GetClient().get();
    }

    sse_client* client_;
};

// Test listing prompts
TEST_F(PromptsTest, ListPrompts) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Call list prompts method directly (using send_request as sse_client doesn't have list_prompts helper yet)
    json prompts_list = client_->send_request("prompts/list").result;
    
    // Verify prompts list
    EXPECT_TRUE(prompts_list.contains("prompts"));
    EXPECT_EQ(prompts_list["prompts"].size(), 1);
    EXPECT_EQ(prompts_list["prompts"][0]["name"], "test_prompt");
    EXPECT_EQ(prompts_list["prompts"][0]["description"], "A test prompt");
    EXPECT_TRUE(prompts_list["prompts"][0].contains("arguments"));
    EXPECT_EQ(prompts_list["prompts"][0]["arguments"][0]["name"], "name");
}

// Test getting prompt
TEST_F(PromptsTest, GetPrompt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Get prompt
    json prompt_result = client_->send_request("prompts/get", {{"name", "test_prompt"}, {"arguments", {{"name", "Alice"}}}}).result;
    
    // Verify prompt result
    EXPECT_TRUE(prompt_result.contains("messages"));
    EXPECT_EQ(prompt_result["messages"].size(), 1);
    EXPECT_EQ(prompt_result["messages"][0]["role"], "user");
    EXPECT_EQ(prompt_result["messages"][0]["content"]["text"], "Hello, Alice!");
}
