using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Экземпляр класса для мертвых, содержит положение, угол, цвет(тест).
/// </summary>
public class unit200 : MonoBehaviour {
    
    public Vector3 position;

    public int command;

    public unit200(Vector3 pos, int c)
    {
        position = pos;
        command = c;
    }

	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void Update () {
		
	}
}
