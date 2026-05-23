#include "usermodel.hpp"
#include "groupmodel.hpp"
#include "friendmodel.hpp"
#include "offlinemessagemodel.hpp"
#include <iostream>
#include <cassert>

using namespace std;

// Unit test for Models to ensure no circular dependency and RAII works.

int main() {
    cout << "Model tests executable loaded." << endl;
    
    // We cannot run real DB queries without a MySQL server.
    // But instantiating them will test for infinite loops (like the circular dependency we fixed).
    
    UserModel userModel;
    GroupModel groupModel;
    FriendModel friendModel;
    OfflineMsgModel offlineMsgModel;
    (void)offlineMsgModel;
    
    User u;
    u.setId(999);
    // Since we don't have DB, insert will return false.
    bool res = userModel.insert(u);
    assert(res == false); // Connection pool is not initialized, so it returns false.
    
    cout << "Model tests passed successfully (Mocked offline)." << endl;
    return 0;
}
