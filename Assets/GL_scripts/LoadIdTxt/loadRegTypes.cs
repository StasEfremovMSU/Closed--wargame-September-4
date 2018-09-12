using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class loadRegTypes 
{
    int temp;

    // Id построения
    public int[] ID;

    // Число солlат в первом ряду
    public int[] wide;

    // Расстояние между солдатами в полку
    public float[] dist;

    // Число солдат в полку
    public int[] N;

    public loadRegTypes(int n)
    {
        temp = 0;
        ID = new int[n];
        wide = new int[n];
        dist = new float[n];
        N = new int[n];
    }

    public void addRegTypes(string[] a)
    {
        Debug.Log(" addItem ");

        ID[temp] = int.Parse(a[0]);

        wide[temp] = int.Parse(a[1]);

        dist[temp] = float.Parse(a[2]);

        N[temp] = int.Parse(a[3]);


        temp++;
    }

}