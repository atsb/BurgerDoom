#version 330 core

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    const vec2 positions[6] = vec2[6](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );
    const vec2 texCoords[6] = vec2[6](
        vec2(0.0, 1.0),
        vec2(1.0, 1.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(0.0, 0.0)
    );

    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    vColor = vec4(1.0);
    vTexCoord = texCoords[gl_VertexID];
}
