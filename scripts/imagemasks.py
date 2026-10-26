"""test scene 의 capture 에서 물체마다 그 물체의 pixel 을 고르는 mask.

capture 에서 물체를 나누는 방식이 script 마다 달라지지 않도록 여기에 모은다. mask 는 (height, width) 의 bool 배열이다.
"""

import numpy as np


def eroded(mask, pixels):
    for _ in range(pixels):
        inner = mask.copy()
        inner[1:, :] &= mask[:-1, :]
        inner[:-1, :] &= mask[1:, :]
        inner[:, 1:] &= mask[:, :-1]
        inner[:, :-1] &= mask[:, 1:]
        inner[[0, -1], :] = False
        inner[:, [0, -1]] = False
        mask = inner
    return mask


def column_groups(mask):
    """mask 를 pixel 이 있는 column 이 이어진 덩어리로 나눈 mask 의 list. 왼쪽부터다."""
    columns = np.concatenate([[False], mask.any(axis=0), [False]])
    edges = np.flatnonzero(columns[1:] != columns[:-1])
    groups = []
    for start, end in zip(edges[::2], edges[1::2]):
        group = np.zeros_like(mask)
        group[:, start:end] = mask[:, start:end]
        groups.append(group)
    return groups
