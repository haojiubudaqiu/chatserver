#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

class UserModel {
public:
    UserModel();

    bool insert(User &user);
    bool insertWithId(User &user);
    User query(int id);
    User query(int id, bool forceMaster);
    User queryByName(const string& name);
    bool updateState(User user);
    void resetState();
};

#endif