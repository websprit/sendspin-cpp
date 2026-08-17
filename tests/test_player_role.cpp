// Copyright 2026 Sendspin Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "player_role_impl.h"
#include "protocol_messages.h"

#include <gtest/gtest.h>

#include <vector>

namespace sendspin {
namespace {

PlayerRoleConfig player_config() {
    PlayerRoleConfig config;
    config.audio_formats = {{SendspinCodecFormat::FLAC, 2, 48000, 16}};
    return config;
}

TEST(PlayerRole, StateKeepsHelloCommandsWhenStaticDelayIsAdjustable) {
    PlayerRole::Impl role(player_config(), nullptr, nullptr);

    ClientHelloMessage hello;
    role.build_hello_fields(hello);
    ASSERT_TRUE(hello.player_v1_support.has_value());
    EXPECT_EQ(hello.player_v1_support->supported_commands,
              (std::vector{SendspinPlayerCommand::VOLUME, SendspinPlayerCommand::MUTE}));

    role.static_delay_adjustable.store(true, std::memory_order_relaxed);
    ClientStateMessage state;
    role.build_state_fields(state);

    ASSERT_TRUE(state.player.has_value());
    EXPECT_EQ(state.player->supported_commands,
              (std::vector{SendspinPlayerCommand::VOLUME, SendspinPlayerCommand::MUTE,
                           SendspinPlayerCommand::SET_STATIC_DELAY}));
}

TEST(PlayerRole, StateKeepsHelloCommandsWhenStaticDelayIsFixed) {
    PlayerRole::Impl role(player_config(), nullptr, nullptr);

    ClientStateMessage state;
    role.build_state_fields(state);

    ASSERT_TRUE(state.player.has_value());
    EXPECT_EQ(state.player->supported_commands,
              (std::vector{SendspinPlayerCommand::VOLUME, SendspinPlayerCommand::MUTE}));
}

}  // namespace
}  // namespace sendspin
