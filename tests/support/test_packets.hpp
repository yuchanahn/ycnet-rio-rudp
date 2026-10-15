#pragma once
#include "ycnet/packet.hpp"

#pragma pack(push, 1)

ycnet_reliable_packet(
    chat,
    char message[256];
);

ycnet_reliable_variable_packet(
  chat_var,
  char, message[512];
);

#pragma pack(pop)