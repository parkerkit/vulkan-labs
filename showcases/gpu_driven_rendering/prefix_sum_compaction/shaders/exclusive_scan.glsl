shared uint scan_values[k_block_size];

uint ExclusiveScan(uint value, out uint total) {
    uint lane = gl_LocalInvocationIndex;
    scan_values[lane] = value;
    barrier();

    // Blelloch 上扫，根节点得到整组的和。
    for (uint stride = 1u; stride < k_block_size; stride *= 2u) {
        uint right = (lane + 1u) * stride * 2u - 1u;
        if (right < k_block_size) {
            scan_values[right] += scan_values[right - stride];
        }
        barrier();
    }

    total = 0u;
    if (lane == 0u) {
        total = scan_values[k_block_size - 1u];
        scan_values[k_block_size - 1u] = 0u;
    }
    barrier();

    // 下扫把每个位置改为它之前所有输入的和。
    for (uint stride = k_block_size / 2u; stride > 0u; stride /= 2u) {
        uint right = (lane + 1u) * stride * 2u - 1u;
        if (right < k_block_size) {
            uint left = right - stride;
            uint subtree_total = scan_values[left];
            scan_values[left] = scan_values[right];
            scan_values[right] += subtree_total;
        }
        barrier();
    }
    return scan_values[lane];
}
