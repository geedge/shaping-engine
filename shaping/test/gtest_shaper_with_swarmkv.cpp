#include <gtest/gtest.h>

TEST(single_session, generic_profile)
{}

int main(int argc, char **argv) 
{
    testing::InitGoogleTest(&argc, argv);
    //testing::GTEST_FLAG(filter) = "single_session.udp_tx_in_order";
    return RUN_ALL_TESTS();
}