#include "../include/interrupt.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

class InterruptTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化中断系统
        Interrupt_Init();
    }

    void TearDown() override {
        // 清理，停止时钟
        InterruptTool::stopTimer();
    }
};

// 测试中断初始化
TEST_F(InterruptTest, InitializationTest) {
    // 检查中断向量表是否正确初始化
    EXPECT_TRUE(InterruptTool::isValid(InterruptType::TIMER));
    EXPECT_FALSE(InterruptTool::isValid(InterruptType::SOFTWARE));
}

// 测试中断使能和禁用
TEST_F(InterruptTest, EnableDisableTest) {
    // 设置某个中断有效
    InterruptTool::setValid(InterruptType::SOFTWARE);
    EXPECT_TRUE(InterruptTool::isValid(InterruptType::SOFTWARE));
    
    // 禁用该中断
    InterruptTool::unsetValid(InterruptType::SOFTWARE);
    EXPECT_FALSE(InterruptTool::isValid(InterruptType::SOFTWARE));
}

// 测试中断优先级
TEST_F(InterruptTest, PriorityTest) {
    // 设置优先级
    InterruptTool::setPriority(InterruptType::TIMER, 1);
    InterruptTool::setPriority(InterruptType::SOFTWARE, 2);
    
    // 触发两个中断
    raiseInterrupt(InterruptType::SOFTWARE, 0, 0, "", nullptr, 0);
    raiseInterrupt(InterruptType::TIMER, 0, 0, "", nullptr, 0);
    
    // TIMER应该先被处理，因为优先级更高
    auto queue = InterruptTool::getInterruptQueue();
    EXPECT_FALSE(queue.empty());
    if (!queue.empty()) {
        EXPECT_EQ(queue.front().type, "TIMER");
    }
}

// 测试不可屏蔽中断
TEST_F(InterruptTest, NonMaskableTest) {
    // 尝试禁用不可屏蔽中断
    InterruptTool::unsetValid(InterruptType::NON_MASKABLE);
    // 不可屏蔽中断应该始终有效
    EXPECT_TRUE(InterruptTool::isValid(InterruptType::NON_MASKABLE));
}

// 测试中断命令解析
TEST_F(InterruptTest, CommandSplitTest) {
    std::string cmd = "create file test.txt";
    std::vector<std::string> args;
    CmdSplit(cmd, args);
    
    EXPECT_EQ(args.size(), 3);
    if (args.size() >= 3) {
        EXPECT_EQ(args[0], "create");
        EXPECT_EQ(args[1], "file");
        EXPECT_EQ(args[2], "test.txt");
    }
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}