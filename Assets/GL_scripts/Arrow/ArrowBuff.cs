using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class ArrowBuff {

    static public List<ArchBuf> archBuf;


    static public void put(ArchBuf x)
    {
        archBuf.Add(x);
    }


    static public void mySort()
    {
        archBuf.Sort(delegate(ArchBuf x, ArchBuf y)
        {

            if (x.time < y.time) return -1;
            if (x.time > y.time) return 1;

            return 0;
        });

    }

    static public void Delete()
    {
        archBuf.Clear();
    }
    
}


public struct ArchBuf 
{
    // Начальное положение
    public Vector3 stPos;

    // Направлеение
    public Quaternion forw;
    
    // Скорость
    public float speed;

    public float time;

}