#include <gtest/gtest.h>

#include "bt/Status.h"

TEST(Step1_Status, ValuesAreDistinct) {
    EXPECT_NE(bt::Status::SUCCESS, bt::Status::FAILURE);
    EXPECT_NE(bt::Status::SUCCESS, bt::Status::RUNNING);
    EXPECT_NE(bt::Status::FAILURE, bt::Status::RUNNING);
}

TEST(Step1_Status, UnderlyingValuesAreStable) {
    EXPECT_EQ(static_cast<int>(bt::Status::SUCCESS), 0);
    EXPECT_EQ(static_cast<int>(bt::Status::FAILURE), 1);
    EXPECT_EQ(static_cast<int>(bt::Status::RUNNING), 2);
}

TEST(Step1_Status, ToStringSuccess) {
    EXPECT_EQ(bt::toString(bt::Status::SUCCESS), "SUCCESS");
}

TEST(Step1_Status, ToStringFailure) {
    EXPECT_EQ(bt::toString(bt::Status::FAILURE), "FAILURE");
}

TEST(Step1_Status, ToStringRunning) {
    EXPECT_EQ(bt::toString(bt::Status::RUNNING), "RUNNING");
}
