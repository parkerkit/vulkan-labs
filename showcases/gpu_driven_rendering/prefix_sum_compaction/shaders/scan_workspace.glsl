const uint k_block_size = 64u;

// 同一 SSBO 依次存放 N 个标记、N 个局部偏移、G 个组总数、G 个组偏移。
uint BlockCount(uint object_count) {
    return (object_count + k_block_size - 1u) / k_block_size;
}

uint LocalOffsetIndex(uint object_count, uint object_index) {
    return object_count + object_index;
}

uint BlockTotalIndex(uint object_count, uint block_index) {
    return object_count * 2u + block_index;
}

uint BlockOffsetIndex(uint object_count, uint block_index) {
    return BlockTotalIndex(object_count, block_index) + BlockCount(object_count);
}
