// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0 <0.9.0;

contract NationalityVerification {


    event NationalityRecorded(address indexed user, string nationality);
    event VerificationAttempted(address indexed user, bool success);
    event AdminAdded(address indexed admin);
    event AdminRemoved(address indexed admin);
    struct User {
        string name;
        string nationality;
        uint8 age;
        bool isVerified;
        bool isActive; 
    }

    // 存储用户的国籍信息
    mapping (address => User) private users;
    address[] private userList;
    address[] private admins; // 管理员列表
    
    
    modifier onlyAdmin() {
        bool isAdmin = false;
        for (uint i = 0; i < admins.length; i++) {
            if (admins[i] == msg.sender) {
                isAdmin = true;
                break;
            }
        }
        require(isAdmin, "Not an admin.");
        _;
    }
    modifier onlyUnregistered() {
        require(bytes(users[msg.sender].name).length == 0, "User already registered.");
        _;
    }

    modifier onlyRegistered() {
        require(bytes(users[msg.sender].name).length != 0, "User not registered.");
        _;
    }

    
    function initialize(address _initialAdmins) public {
        admins.push(_initialAdmins);
    }

    function addAdmin(address _admin) public onlyAdmin {
        for (uint i = 0; i < admins.length; i++) {
            if (admins[i] == _admin) {
                revert("Admin already exists.");
            }
        }
        admins.push(_admin);
        emit AdminAdded(_admin);
    }
    
    function removeAdmin(address _admin) public onlyAdmin {
        for (uint i = 0; i < admins.length; i++) {
            if (admins[i] == _admin) {
                admins[i] = admins[admins.length - 1];
                admins.pop();
                emit AdminRemoved(_admin);
                break;
            }
        }
    }

    // 用户注册国籍信息
    function register(string memory _name, string memory _nationality, uint8 _age) public onlyUnregistered {
        require(bytes(_name).length > 0 && bytes(_nationality).length > 0, "Name and nationality cannot be empty.");
        users[msg.sender] = User({
            name: _name,
            nationality: _nationality,
            age: _age,
            isVerified: false,
            isActive: true
        });
        userList.push(msg.sender);
        emit NationalityRecorded(msg.sender, _nationality);
    }

    // 用户更新其国籍信息
    function updateNationality(string memory _nationality, uint8 _age) public onlyRegistered {
        users[msg.sender].nationality = _nationality;
        users[msg.sender].age = _age;
        emit NationalityRecorded(msg.sender, _nationality);
    }

    //管理员验证用户提供的信息
    function verifyUser(address _user) public onlyRegistered {
        require(users[_user].isVerified == false, "User already verified.");
        users[_user].isVerified = true;
        emit VerificationAttempted(_user, true);
    }

    //获取用户的国等信息
    function getUserNationality(address _user) public view returns (string memory, string memory,uint8 array, bool, bool) {
        return (users[_user].name, users[_user].nationality, users[_user].age, users[_user].isVerified, users[_user].isActive); 
    }

    function deleteUser(address _user) public onlyAdmin {
        for (uint i = 0; i < userList.length; i++) {
            if (userList[i] == msg.sender) {
                users[_user].isActive=false;
                userList[i] = userList[userList.length - 1];
                userList.pop();
                break;
            }
        }
    }

    // 检查用户是否已被验证
    function isUserVerified(address _user,bool _approve) public returns (bool) {
        if (users[_user].isVerified == true) {
            emit VerificationAttempted(_user, true); // 用户已经被验证
        } else {
            if (_approve) {
                users[_user].isVerified = true;
                emit VerificationAttempted(_user, true);
            } else {
                emit VerificationAttempted(_user, false); // 拒绝了验证请求
            }
        }
    }

    // 计算用户列表长度
    function getUsersCount() public view returns (uint) {
        return userList.length;
    }
}