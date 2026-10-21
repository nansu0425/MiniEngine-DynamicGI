#pragma once

class Model;

// mesh마다 BLAS를 하나씩 build해 model에 둔다.
// skinned mesh는 vertex가 animation을 따라 바뀌므로 BLAS를 만들지 않는다.
void BuildBottomLevelAS(Model& model);

// ray가 맞은 BLAS geometry의 triangle과 material을 HitSurface.hlsli가 읽을 수 있도록
// model의 m_BLASGeometries와 SRV를 만든다. BuildBottomLevelAS 다음에 부른다.
void CreateBLASGeometries(Model& model);
