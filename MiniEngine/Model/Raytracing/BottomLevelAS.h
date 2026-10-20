#pragma once

class Model;

// mesh마다 BLAS를 하나씩 build해 model에 둔다.
// skinned mesh는 vertex가 animation을 따라 바뀌므로 BLAS를 만들지 않는다.
void BuildBottomLevelAS(Model& model);
